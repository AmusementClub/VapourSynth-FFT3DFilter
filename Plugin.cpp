/*****************************************************************************
 * Plugin.cpp
 *****************************************************************************
 * Copyright (C) 2015
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

#include "FFT3DFilter.h"
#include <VapourSynth4.h>
#include <VSHelper4.h>
#include <string>
#include <stdexcept>

static inline void getPlanesArg(const VSMap *in, bool *process, const VSAPI *vsapi) {
    int m = vsapi->mapNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        process[i] = (m <= 0);

    for (int i = 0; i < m; i++) {
        int o = vsapi->mapGetIntSaturated(in, "planes", i, nullptr);

        if (o < 0 || o >= 3)
            throw std::runtime_error("plane index out of range");

        if (process[o])
            throw std::runtime_error("plane specified twice");

        process[o] = true;
    }
}

static inline void set_option_int
(
    int *opt,
    int      default_value,
    const char *arg,
    const VSMap *in,
    const VSAPI *vsapi
) {
    int e;
    *opt = vsapi->mapGetIntSaturated(in, arg, 0, &e);
    if (e)
        *opt = default_value;
}

static inline void set_option_float
(
    float *opt,
    float        default_value,
    const char *arg,
    const VSMap *in,
    const VSAPI *vsapi
) {
    int e;
    *opt = vsapi->mapGetFloatSaturated(in, arg, 0, &e);
    if (e)
        *opt = static_cast<float>(default_value);
}

static void VS_CC createFFT3DFilter
(
    const VSMap *in,
    VSMap *out,
    void *user_data,
    VSCore *core,
    const VSAPI *vsapi
) {
    float sigma1;
    float beta;
    bool process[3];
    int bw;
    int bh;
    int bt;
    int ow;
    int oh;
    float   kratio;
    float   sharpen;
    float   scutoff;
    float   svr;
    float   smin;
    float   smax;
    int measure;
    int interlaced;
    int wintype;
    int pframe;
    int px;
    int py;
    int pshow;
    float   pcutoff;
    float   pfactor;
    float   sigma2;
    float   sigma3;
    float   sigma4;
    float   degrid;
    float   dehalo;
    float   hr;
    float   ht;
    int ncpu;

    try {
        int istat = fftwf_init_threads();
        if (istat == 0)
            throw std::runtime_error{ "fftwf_init_threads() failed!" };

        fftwf_make_planner_thread_safe();

        getPlanesArg(in, process, vsapi);

        set_option_float(&sigma1, 2.0, "sigma", in, vsapi);
        set_option_float(&beta, 1.0, "beta", in, vsapi);
        set_option_int(&bw, 32, "bw", in, vsapi);
        set_option_int(&bh, 32, "bh", in, vsapi);
        set_option_int(&bt, 3, "bt", in, vsapi);
        set_option_int(&ow, bw / 3, "ow", in, vsapi);
        set_option_int(&oh, bh / 3, "oh", in, vsapi);
        set_option_float(&kratio, 2.0, "kratio", in, vsapi);
        set_option_float(&sharpen, 0, "sharpen", in, vsapi);
        set_option_float(&scutoff, 0.3f, "scutoff", in, vsapi);
        set_option_float(&svr, 1.0, "svr", in, vsapi);
        set_option_float(&smin, 4.0, "smin", in, vsapi);
        set_option_float(&smax, 20.0, "smax", in, vsapi);
        set_option_int(&measure, 1, "measure", in, vsapi);
        set_option_int(&interlaced, 0, "interlaced", in, vsapi);
        set_option_int(&wintype, 0, "wintype", in, vsapi);
        set_option_int(&pframe, 0, "pframe", in, vsapi);
        set_option_int(&px, 0, "px", in, vsapi);
        set_option_int(&py, 0, "py", in, vsapi);
        set_option_int(&pshow, 0, "pshow", in, vsapi);
        set_option_float(&pcutoff, 0.1f, "pcutoff", in, vsapi);
        set_option_float(&pfactor, 0, "pfactor", in, vsapi);
        set_option_float(&sigma2, sigma1, "sigma2", in, vsapi);
        set_option_float(&sigma3, sigma1, "sigma3", in, vsapi);
        set_option_float(&sigma4, sigma1, "sigma4", in, vsapi);
        set_option_float(&degrid, 1.0, "degrid", in, vsapi);
        set_option_float(&dehalo, 0, "dehalo", in, vsapi);
        set_option_float(&hr, 2.0, "hr", in, vsapi);
        set_option_float(&ht, 50.0, "ht", in, vsapi);
        set_option_int(&ncpu, 1, "ncpu", in, vsapi);

        if (bt < -1 || bt > 5)
            throw std::runtime_error{ "bt must be -1(Sharpen), 0(Kalman) or 1,2,3,4,5(Wiener)" };
        if (ow * 2 > bw)
            throw std::runtime_error{ "Must not be 2*ow > bw" };
        if (oh * 2 > bh)
            throw std::runtime_error{ "Must not be 2*oh > bh" };
        if (beta < 1)
            throw std::runtime_error{ "beta must be no less than 1.0" };

        VSNode *node = vsapi->mapGetNode(in, "clip", 0, nullptr);
        const VSVideoInfo *vi = vsapi->getVideoInfo(node);

        if ((vi->format.sampleType == stFloat && vi->format.bitsPerSample != 32) || (vi->format.sampleType == stInteger && vi->format.bitsPerSample > 16) || !vsh::isConstantVideoFormat(vi)) {
            vsapi->freeNode(node);
            throw std::runtime_error{ "Input clip must be 8-16 bit integer or 32 bit float constant format" };
        }

        int num_process = 0;
        for (int i = 0; i < vi->format.numPlanes; i++)
            if (process[i])
                num_process++;

        if (num_process < 1) {
            vsapi->freeNode(node);
            throw std::runtime_error{ "No planes to process" };
        }

        if (pshow && pfactor != 0) {
            int plane = 0;

            VSMap *tmp = vsapi->createMap();

            FFT3DFilterTransform *pshowtransform = new FFT3DFilterTransform(true, node, plane, wintype, bw, bh, ow, oh, px, py, pcutoff, degrid, interlaced, measure, ncpu, core, vsapi);

            VSFilterDependency deps1[] = {{node, 1}};
            VSNode *pshownode = vsapi->createVideoFilter2
            (
                "FFT3DFilterHelper",
                vi,
                FFT3DFilterTransform::GetPShowFrame,
                FFT3DFilterTransform::Free,
                fmParallelRequests, deps1, 1, pshowtransform, core
            );

            FFT3DFilterPShow *pshowfilter = new FFT3DFilterPShow(pshownode, plane, bw, bh, ow, oh, interlaced, core, vsapi);

            VSFilterDependency deps2[] = {{pshownode, 1}};
            vsapi->createVideoFilter
            (
                tmp,
                "FFT3DFilterPShow",
                vi,
                FFT3DFilterPShow::GetFrame,
                FFT3DFilterPShow::Free,
                fmParallelRequests, deps2,
                1, pshowfilter, core
            );

            vsapi->mapSetData(tmp, "props", "px", -1, dtUtf8, maAppend);
            vsapi->mapSetData(tmp, "props", "py", -1, dtUtf8, maAppend);
            vsapi->mapSetData(tmp, "props", "sigma", -1, dtUtf8, maAppend);

            VSMap *tmp2 = vsapi->invoke(vsapi->getPluginByID("com.vapoursynth.text", core), "FrameProps", tmp);
            VSNode *textnode = vsapi->mapGetNode(tmp2, "clip", 0, nullptr);
            vsapi->mapSetNode(out, "clip", textnode, maAppend);
            vsapi->freeNode(textnode);
            vsapi->freeMap(tmp2);

            vsapi->freeMap(tmp);
        } else {
            VSMap *tmp = vsapi->createMap();
            VSNode *outnodes[3] = {};

            for (int plane = 0; plane < vi->format.numPlanes; plane++) {
                if (process[plane]) {

                    FFT3DFilterTransform *transform = new FFT3DFilterTransform(false, vsapi->addNodeRef(node), plane, wintype, bw, bh, ow, oh, px, py, pcutoff, degrid, interlaced, measure, ncpu, core, vsapi);

                    VSFilterDependency deps1[] = {{node, 1}};
                    VSNode *transformednode = vsapi->createVideoFilter2
                    (
                        ("FFT3DFilterTrans" + std::to_string(plane)).c_str(),
                        transform->GetOutputVI(),
                        FFT3DFilterTransform::GetFrame,
                        FFT3DFilterTransform::Free,
                        fmParallelRequests, deps1, 1, transform, core
                    );

                    FFT3DFilter *mainFilter = new FFT3DFilter(transform, vi, sigma1, beta, plane, bw, bh, bt, ow, oh,
                        kratio, sharpen, scutoff, svr, smin, smax,
                        pframe, px, py, pshow, pcutoff, pfactor,
                        sigma2, sigma3, sigma4, degrid, dehalo, hr, ht, ncpu,
                        transformednode, core, vsapi);


                    VSFilterDependency deps2[] = {{transformednode, bt <= 1}};
                    VSNode *mainnode = vsapi->createVideoFilter2
                    (
                        ("FFT3DFilterMain" + std::to_string(plane)).c_str(),
                        vsapi->getVideoInfo(transformednode),
                        FFT3DFilter::GetFrame,
                        FFT3DFilter::Free,
                        bt == 0 ? fmParallelRequests : fmParallel, deps2, 1, mainFilter, core
                    );


                    FFT3DFilterInvTransform *invtransform = new FFT3DFilterInvTransform(mainnode, vi, plane, wintype, bw, bh, ow, oh, interlaced, measure, ncpu, core, vsapi);

                    VSFilterDependency deps3[] = {{mainnode, 1}};
                    vsapi->createVideoFilter
                    (
                        vi->format.numPlanes == 1 ? out : tmp,
                        ("FFT3DFilterInv" + std::to_string(plane)).c_str(),
                        invtransform->GetOutputVI(),
                        FFT3DFilterInvTransform::GetFrame,
                        FFT3DFilterInvTransform::Free,
                        fmParallelRequests, deps3, 1, invtransform, core
                    );

                    if (vi->format.numPlanes > 1) {
                        outnodes[plane] = vsapi->mapGetNode(tmp, "clip", 0, nullptr);
                        vsapi->clearMap(tmp);
                    }
                } else {
                    outnodes[plane] = vsapi->addNodeRef(node);
                }
            }

            vsapi->freeNode(node);

            if (vi->format.numPlanes > 1) {
                for (int plane = 0; plane < vi->format.numPlanes; plane++)
                    vsapi->mapConsumeNode(tmp, "clips", outnodes[plane], maAppend);
                int64_t pvals[] = { 0, process[1] ? 0 : 1, process[2] ? 0 : 2 };
                vsapi->mapSetIntArray(tmp, "planes", pvals, 3);
                vsapi->mapSetInt(tmp, "colorfamily", vi->format.colorFamily, maAppend);
                VSMap *tmp2 = vsapi->invoke(vsapi->getPluginByID("com.vapoursynth.std", core), "ShufflePlanes", tmp);
                vsapi->mapConsumeNode(out, "clip", vsapi->mapGetNode(tmp2, "clip", 0, nullptr), maAppend);
                vsapi->freeMap(tmp2);
            }

            vsapi->freeMap(tmp);
        }
    } catch (std::runtime_error &e) {
        vsapi->mapSetError(out, (std::string("FFT3DFilter: ") + e.what()).c_str());
    }
}

//fixme, make interlaced handling based on field based property and per frame

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("systems.innocent.fft3dfilter", "fft3dfilter", "systems", VS_MAKE_VERSION(2, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("FFT3DFilter",
        "clip:vnode;sigma:float:opt;beta:float:opt;planes:int[]:opt;bw:int:opt;bh:int:opt;bt:int:opt;ow:int:opt;oh:int:opt;"
        "kratio:float:opt;sharpen:float:opt;scutoff:float:opt;svr:float:opt;smin:float:opt;smax:float:opt;"
        "measure:int:opt;interlaced:int:opt;wintype:int:opt;"
        "pframe:int:opt;px:int:opt;py:int:opt;pshow:int:opt;pcutoff:float:opt;pfactor:float:opt;"
        "sigma2:float:opt;sigma3:float:opt;sigma4:float:opt;degrid:float:opt;"
        "dehalo:float:opt;hr:float:opt;ht:float:opt;ncpu:int:opt;",
        "clip:vnode;",
        createFFT3DFilter, nullptr, plugin);
}
