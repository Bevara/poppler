/*
 *			GPAC - Multimedia Framework C SDK
 *
 *  This file is part of GPAC / PDF image decoder filter
 *  based on poppler (https://poppler.freedesktop.org/), via its
 *  poppler-cpp wrapper API.
 *
 *  Renders the first page of a PDF file to a raw RGB image (matching
 *  the single-frame "img" convention used by the other image decoder
 *  filters in this repo) - a PDF document as a whole is a page
 *  sequence rather than a single image, but page 0 covers the common
 *  "preview/thumbnail a PDF" use case this filter targets.
 */

#include <memory>

/* gpac/filters.h wraps its own #include <emscripten/emscripten.h> inside
 * an `extern "C" {` block (for the plain-C filters everywhere else in
 * this repo, where that's a no-op). emscripten.h's __em_asm_sig<T>
 * template specializations require C++ linkage, so under a .cpp
 * translation unit that combination fails to compile. Including it here
 * first (its own #pragma once then makes filters.h's copy a no-op) keeps
 * it outside any extern "C" block. */
#include <emscripten/emscripten.h>

#include <gpac/filters.h>
#include <gpac/constants.h>

#include <poppler-document.h>
#include <poppler-page.h>
#include <poppler-page-renderer.h>
#include <poppler-image.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
} GF_PDFDecCtx;

/* PROP_UINT()/PROP_BOOL() rely on C99 compound-literal-as-lvalue
 * semantics (`&(GF_PropertyValue){...}`), which standard C++ does not
 * have (only clang's non-standard extension gives it a value, never an
 * addressable lvalue) - this small wrapper sidesteps that by giving the
 * GF_PropertyValue a real (function-local) storage duration instead. */
static void pdfdec_set_uint_prop(GF_FilterPid *pid, u32 prop_code, u32 val)
{
	GF_PropertyValue pv;
	pv.type = GF_PROP_UINT;
	pv.value.uint = val;
	gf_filter_pid_set_property(pid, prop_code, &pv);
}

static GF_Err pdfdec_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	GF_PDFDecCtx *ctx = (GF_PDFDecCtx *)gf_filter_get_udta(filter);

	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}

	pdfdec_set_uint_prop(ctx->opid, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL);
	pdfdec_set_uint_prop(ctx->opid, GF_PROP_PID_CODECID, GF_CODECID_RAW);
	pdfdec_set_uint_prop(ctx->opid, GF_PROP_PID_PIXFMT, GF_PIXEL_RGB);

	return GF_OK;
}

static Bool pdfdec_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_PDFDecCtx *ctx = (GF_PDFDecCtx *)gf_filter_get_udta(filter);
	switch (evt->base.type)
	{
	case GF_FEVT_PLAY:
		ctx->is_playing = GF_TRUE;
		return GF_FALSE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		return GF_FALSE;
	}
}

static GF_Err pdfdec_process(GF_Filter *filter)
{
	GF_FilterPacket *pck;
	u8 *data;
	u32 size;
	GF_PDFDecCtx *ctx = (GF_PDFDecCtx *)gf_filter_get_udta(filter);

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (u8 *)gf_filter_pck_get_data(pck, &size);
	if (!data)
	{
		gf_filter_pid_drop_packet(ctx->ipid);
		return GF_IO_ERR;
	}

	std::unique_ptr<poppler::document> doc(poppler::document::load_from_raw_data((const char *)data, (int)size));
	gf_filter_pid_drop_packet(ctx->ipid);

	if (!doc || doc->is_locked() || (doc->pages() < 1))
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[PDFDec] Failed to load PDF document\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	std::unique_ptr<poppler::page> pg(doc->create_page(0));
	if (!pg)
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[PDFDec] Failed to open page 0\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	poppler::page_renderer renderer;
	renderer.set_image_format(poppler::image::format_rgb24);
	poppler::image img = renderer.render_page(pg.get());

	if (!img.is_valid() || (img.width() <= 0) || (img.height() <= 0))
	{
		GF_LOG(GF_LOG_ERROR, GF_LOG_CODEC, ("[PDFDec] Failed to render page 0\n"));
		return GF_NON_COMPLIANT_BITSTREAM;
	}

	{
		u32 w = (u32)img.width();
		u32 h = (u32)img.height();
		u32 src_stride = (u32)img.bytes_per_row();
		u32 dst_stride = w * 3;
		u32 out_size = dst_stride * h;
		u8 *output;
		GF_FilterPacket *dst_pck;
		const char *src = img.const_data();
		u32 y;

		pdfdec_set_uint_prop(ctx->opid, GF_PROP_PID_WIDTH, w);
		pdfdec_set_uint_prop(ctx->opid, GF_PROP_PID_HEIGHT, h);
		pdfdec_set_uint_prop(ctx->opid, GF_PROP_PID_STRIDE, dst_stride);

		dst_pck = gf_filter_pck_new_alloc(ctx->opid, out_size, &output);
		if (!dst_pck) return GF_OUT_OF_MEM;

		for (y = 0; y < h; y++)
		{
			memcpy(output + y * dst_stride, src + y * src_stride, dst_stride);
		}

		gf_filter_pck_set_cts(dst_pck, 0);
		gf_filter_pck_set_sap(dst_pck, GF_FILTER_SAP_1);
		gf_filter_pck_send(dst_pck);
	}

	gf_filter_pid_set_eos(ctx->opid);
	return GF_EOS;
}

static void pdfdec_finalize(GF_Filter *filter)
{
}

static const GF_FilterCapability PDFDecCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "pdf"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "application/pdf"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

GF_FilterRegister PDFDecoderRegister = {
	.name = "pdfdec",
	GF_FS_SET_DESCRIPTION("PDF image decoder")
		GF_FS_SET_HELP("This filter renders the first page of a PDF file to a raw RGB image using poppler.")
			.private_size = sizeof(GF_PDFDecCtx),
	SETCAPS(PDFDecCaps),
	.configure_pid = pdfdec_configure_pid,
	.process = pdfdec_process,
	.process_event = pdfdec_process_event,
	.finalize = pdfdec_finalize,
};

extern "C" {

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE dynCall_pdfdec_register(GF_FilterSession *session)
{
	return &PDFDecoderRegister;
}

#include "filter_register.h"
__attribute__((constructor))
void register_pdfdec(void) {
    gf_filter_auto_register("pdfdec", dynCall_pdfdec_register);
}

}
