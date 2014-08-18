/*
**   VapourSynth port by HolyWu
**
**   Modification of Fizick's yadif avisynth filter.
**   Copyright (C) 2007 Kevin Stone
**   Yadif C-plugin for Avisynth 2.5 - Yet Another DeInterlacing Filter
**   Copyright (C) 2007 Alexander G. Balakhnin aka Fizick  http://avisynth.org.ru
**   Port of YADIF filter from MPlayer
**   Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
**
**   This program is free software; you can redistribute it and/or modify
**   it under the terms of the GNU General Public License as published by
**   the Free Software Foundation.
**
**   This program is distributed in the hope that it will be useful,
**   but WITHOUT ANY WARRANTY; without even the implied warranty of
**   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**   GNU General Public License for more details.
**
**   You should have received a copy of the GNU General Public License
**   along with this program; if not, write to the Free Software
**   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <cstdlib>
#ifdef VS_TARGET_CPU_X86
#include "./vectorclass/vectorclass.h"
#endif
#include "VapourSynth.h"
#include "VSHelper.h"

struct YadifmodData {
    VSNodeRef * node;
    VSNodeRef * edeint;
    VSVideoInfo vi;
    int order, field, mode;
    bool process[3];
};

template<typename T>
static void YadifmodC(const T *prev2pp, const T *prev2pn, const T *prevp2p, const T *prevp, const T *prevp2n, const T *srcpp, const T *srcpn, const T *nextp2p, const T *nextp, const T *nextp2n, const T *next2pp, const T *next2pn, const T *edeintp, T *dstp, int width, int starty, int stopy, int stride, const YadifmodData *d) {
    for (int y = starty; y <= stopy; y += 2) {
        for (int x = 0; x < width; x++) {
            const int p1 = srcpp[x];
            const int p2 = (prevp[x] + nextp[x]) >> 1;
            const int p3 = srcpn[x];
            const int tdiff0 = std::abs(prevp[x] - nextp[x]);
            const int tdiff1 = (std::abs(prev2pp[x] - p1) + std::abs(prev2pn[x] - p3)) >> 1;
            const int tdiff2 = (std::abs(next2pp[x] - p1) + std::abs(next2pn[x] - p3)) >> 1;
            int diff = VSMAX(VSMAX(tdiff0 >> 1, tdiff1), tdiff2);
            if (d->mode < 2) {
                const int p0 = (prevp2p[x] + nextp2p[x]) >> 1;
                const int p4 = (prevp2n[x] + nextp2n[x]) >> 1;
                const int maxs = VSMAX(VSMAX(p2 - p3, p2 - p1), VSMIN(p0 - p1, p4 - p3));
                const int mins = VSMIN(VSMIN(p2 - p3, p2 - p1), VSMAX(p0 - p1, p4 - p3));
                diff = VSMAX(VSMAX(diff, mins), -maxs);
            }
            const int spatialPred = edeintp[x];
            if (spatialPred > p2 + diff)
                dstp[x] = p2 + diff;
            else if (spatialPred < p2 - diff)
                dstp[x] = p2 - diff;
            else
                dstp[x] = spatialPred;
        }
        prev2pp += stride;
        prev2pn += stride;
        prevp2p += stride;
        prevp += stride;
        prevp2n += stride;
        srcpp += stride;
        srcpn += stride;
        nextp2p += stride;
        nextp += stride;
        nextp2n += stride;
        next2pp += stride;
        next2pn += stride;
        edeintp += stride;
        dstp += stride;
    }
}

#ifdef VS_TARGET_CPU_X86
template<typename T, typename V1, typename V2, int VectorSize>
static void YadifmodSIMD(const T *prev2pp, const T *prev2pn, const T *prevp2p, const T *prevp, const T *prevp2n, const T *srcpp, const T *srcpn, const T *nextp2p, const T *nextp, const T *nextp2n, const T *next2pp, const T *next2pn, const T *edeintp, T *dstp, int width, int starty, int stopy, int stride, const YadifmodData *d) {
    for (int y = starty; y <= stopy; y += 2) {
        for (int x = 0; x < width; x += VectorSize) {
            V1 prev2ppV = V1().load_a(prev2pp + x);
            V1 prev2pnV = V1().load_a(prev2pn + x);
            V1 prevp2pV = V1().load_a(prevp2p + x);
            V1 prevpV = V1().load_a(prevp + x);
            V1 prevp2nV = V1().load_a(prevp2n + x);
            V1 srcppV = V1().load_a(srcpp + x);
            V1 srcpnV = V1().load_a(srcpn + x);
            V1 nextp2pV = V1().load_a(nextp2p + x);
            V1 nextpV = V1().load_a(nextp + x);
            V1 nextp2nV = V1().load_a(nextp2n + x);
            V1 next2ppV = V1().load_a(next2pp + x);
            V1 next2pnV = V1().load_a(next2pn + x);
            V1 edeintpV = V1().load_a(edeintp + x);
            if (width - x < VectorSize) {
                prev2ppV.cutoff(width - x);
                prev2pnV.cutoff(width - x);
                prevp2pV.cutoff(width - x);
                prevpV.cutoff(width - x);
                prevp2nV.cutoff(width - x);
                srcppV.cutoff(width - x);
                srcpnV.cutoff(width - x);
                nextp2pV.cutoff(width - x);
                nextpV.cutoff(width - x);
                nextp2nV.cutoff(width - x);
                next2ppV.cutoff(width - x);
                next2pnV.cutoff(width - x);
                edeintpV.cutoff(width - x);
            }

            V2 p1 = V2(extend_low(srcppV), extend_high(srcppV));
            V2 p2 = (V2(extend_low(prevpV), extend_high(prevpV)) + V2(extend_low(nextpV), extend_high(nextpV))) >> 1;
            V2 p3 = V2(extend_low(srcpnV), extend_high(srcpnV));
            V2 tdiff0 = abs(V2(extend_low(prevpV), extend_high(prevpV)) - V2(extend_low(nextpV), extend_high(nextpV)));
            V2 tdiff1 = (abs(V2(extend_low(prev2ppV), extend_high(prev2ppV)) - p1) + abs(V2(extend_low(prev2pnV), extend_high(prev2pnV)) - p3)) >> 1;
            V2 tdiff2 = (abs(V2(extend_low(next2ppV), extend_high(next2ppV)) - p1) + abs(V2(extend_low(next2pnV), extend_high(next2pnV)) - p3)) >> 1;
            V2 diff = max(max(tdiff0 >> 1, tdiff1), tdiff2);
            if (d->mode < 2) {
                V2 p0 = (V2(extend_low(prevp2pV), extend_high(prevp2pV)) + V2(extend_low(nextp2pV), extend_high(nextp2pV))) >> 1;
                V2 p4 = (V2(extend_low(prevp2nV), extend_high(prevp2nV)) + V2(extend_low(nextp2nV), extend_high(nextp2nV))) >> 1;
                V2 maxs = max(max(p2 - p3, p2 - p1), min(p0 - p1, p4 - p3));
                V2 mins = min(min(p2 - p3, p2 - p1), max(p0 - p1, p4 - p3));
                diff = max(max(diff, mins), -maxs);
            }
            V2 spatialPred = V2(extend_low(edeintpV), extend_high(edeintpV));
            V2 result = select(spatialPred > p2 + diff, p2 + diff, select(spatialPred < p2 - diff, p2 - diff, spatialPred));

            if (width - x >= VectorSize)
                V1(compress(result.get_low(), result.get_high())).store_a(dstp + x);
            else
                V1(compress(result.get_low(), result.get_high())).store_partial(width - x, dstp + x);
        }
        prev2pp += stride;
        prev2pn += stride;
        prevp2p += stride;
        prevp += stride;
        prevp2n += stride;
        srcpp += stride;
        srcpn += stride;
        nextp2p += stride;
        nextp += stride;
        nextp2n += stride;
        next2pp += stride;
        next2pn += stride;
        edeintp += stride;
        dstp += stride;
    }
}
#endif

static void VS_CC yadifmodInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    YadifmodData * d = (YadifmodData *)*instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC yadifmodGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    YadifmodData * d = (YadifmodData *)*instanceData;

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->edeint, frameCtx);
        if (d->mode & 1)
            n >>= 1;
        if (n > 0)
            vsapi->requestFrameFilter(n - 1, d->node, frameCtx);
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        if (n < d->vi.numFrames - 1)
            vsapi->requestFrameFilter(n + 1, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef * edeint = vsapi->getFrameFilter(n, d->edeint, frameCtx);
        int fieldt = d->field;
        if (d->mode & 1) {
            fieldt = n & 1 ? 1 - d->order : d->order;
            n >>= 1;
        }
        const VSFrameRef * prv = vsapi->getFrameFilter(n > 0 ? n - 1 : 0, d->node, frameCtx);
        const VSFrameRef * src = vsapi->getFrameFilter(n, d->node, frameCtx);
        const VSFrameRef * nxt = vsapi->getFrameFilter(n < d->vi.numFrames - 1 ? n + 1 : d->vi.numFrames - 1, d->node, frameCtx);
        const VSFrameRef * fr[] = { d->process[0] ? nullptr : src, d->process[1] ? nullptr : src, d->process[2] ? nullptr : src };
        const int pl[] = { 0, 1, 2 };
        VSFrameRef * dst = vsapi->newVideoFrame2(d->vi.format, d->vi.width, d->vi.height, fr, pl, src, core);

        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            if (d->process[plane]) {
                const int width = vsapi->getFrameWidth(src, plane);
                const int height = vsapi->getFrameHeight(src, plane);
                const int stride = vsapi->getStride(src, plane);
                const uint8_t * srcp0 = vsapi->getReadPtr(prv, plane);
                const uint8_t * srcp1 = vsapi->getReadPtr(src, plane);
                const uint8_t * srcp2 = vsapi->getReadPtr(nxt, plane);
                const uint8_t * edeintp = vsapi->getReadPtr(edeint, plane);
                uint8_t * dstp = vsapi->getWritePtr(dst, plane);
                if (!fieldt)
                    vs_bitblt(dstp, stride, srcp1 + stride, stride, width * d->vi.format->bytesPerSample, 1);
                else
                    vs_bitblt(dstp + stride, stride, edeintp + stride, stride, width * d->vi.format->bytesPerSample, 1);
                vs_bitblt(dstp + stride * (1 - fieldt), stride * 2, srcp1 + stride * (1 - fieldt), stride * 2, width * d->vi.format->bytesPerSample, height >> 1);
                const int starty = 2 + fieldt;
                const int stopy = fieldt ? height - 3 : height - 4;
                const int stride1 = stride * starty;
                const uint8_t * prevp, * nextp;
                if (fieldt ^ d->order) {
                    prevp = srcp1 + stride1;
                    nextp = srcp2 + stride1;
                } else {
                    prevp = srcp0 + stride1;
                    nextp = srcp1 + stride1;
                }
                edeintp += stride1;
                dstp += stride1;
                const int stride2 = stride * (starty - 1);
                const uint8_t * prev2pp = srcp0 + stride2;
                const uint8_t * srcpp = srcp1 + stride2;
                const uint8_t * next2pp = srcp2 + stride2;
                const int stride3 = stride * 2;
                const uint8_t * prev2pn = prev2pp + stride3;
                const uint8_t * prevp2p = prevp - stride3;
                const uint8_t * prevp2n = prevp + stride3;
                const uint8_t * srcpn = srcpp + stride3;
                const uint8_t * nextp2p = nextp - stride3;
                const uint8_t * nextp2n = nextp + stride3;
                const uint8_t * next2pn = next2pp + stride3;

                if (d->vi.format->bytesPerSample == 1) {
#ifdef VS_TARGET_CPU_X86
                    YadifmodSIMD<uint8_t, Vec16uc, Vec16s, 16>(prev2pp, prev2pn, prevp2p, prevp, prevp2n, srcpp, srcpn, nextp2p, nextp, nextp2n, next2pp, next2pn, edeintp, dstp, width, starty, stopy, stride3, d);
#else
                    YadifmodC<uint8_t>(prev2pp, prev2pn, prevp2p, prevp, prevp2n, srcpp, srcpn, nextp2p, nextp, nextp2n, next2pp, next2pn, edeintp, dstp, width, starty, stopy, stride3, d);
#endif
                } else if (d->vi.format->bytesPerSample == 2) {
#ifdef VS_TARGET_CPU_X86
                    YadifmodSIMD<uint16_t, Vec8us, Vec8i, 8>((const uint16_t *)prev2pp, (const uint16_t *)prev2pn, (const uint16_t *)prevp2p, (const uint16_t *)prevp, (const uint16_t *)prevp2n, (const uint16_t *)srcpp, (const uint16_t *)srcpn, (const uint16_t *)nextp2p, (const uint16_t *)nextp, (const uint16_t *)nextp2n, (const uint16_t *)next2pp, (const uint16_t *)next2pn, (const uint16_t *)edeintp, (uint16_t *)dstp, width, starty, stopy, stride3 >> 1, d);
#else
                    YadifmodC<uint16_t>((const uint16_t *)prev2pp, (const uint16_t *)prev2pn, (const uint16_t *)prevp2p, (const uint16_t *)prevp, (const uint16_t *)prevp2n, (const uint16_t *)srcpp, (const uint16_t *)srcpn, (const uint16_t *)nextp2p, (const uint16_t *)nextp, (const uint16_t *)nextp2n, (const uint16_t *)next2pp, (const uint16_t *)next2pn, (const uint16_t *)edeintp, (uint16_t *)dstp, width, starty, stopy, stride3 >> 1, d);
#endif
                }

                if (!fieldt)
                    vs_bitblt(vsapi->getWritePtr(dst, plane) + stride * (height - 2), stride, vsapi->getReadPtr(edeint, plane) + stride * (height - 2), stride, width * d->vi.format->bytesPerSample, 1);
                else
                    vs_bitblt(vsapi->getWritePtr(dst, plane) + stride * (height - 1), stride, srcp1 + stride * (height - 2), stride, width * d->vi.format->bytesPerSample, 1);
            }
        }

        vsapi->freeFrame(prv);
        vsapi->freeFrame(src);
        vsapi->freeFrame(nxt);
        vsapi->freeFrame(edeint);
        return dst;
    }

    return nullptr;
}

static void VS_CC yadifmodFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    YadifmodData * d = (YadifmodData *)instanceData;
    vsapi->freeNode(d->node);
    vsapi->freeNode(d->edeint);
    delete d;
}

static void VS_CC yadifmodCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    YadifmodData d;
    YadifmodData * data;
    int err;

    d.order = !!vsapi->propGetInt(in, "order", 0, nullptr);
    d.field = !!vsapi->propGetInt(in, "field", 0, &err);
    if (err)
        d.field = d.order;
    d.mode = int64ToIntS(vsapi->propGetInt(in, "mode", 0, nullptr));

    if (d.mode < 0 || d.mode > 3) {
        vsapi->setError(out, "Yadifmod: mode must be 0, 1, 2, or 3");
        return;
    }

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.edeint = vsapi->propGetNode(in, "edeint", 0, nullptr);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (!isConstantFormat(&d.vi) || !d.vi.numFrames || d.vi.format->sampleType != stInteger || d.vi.format->bytesPerSample > 2) {
        vsapi->setError(out, "Yadifmod: only constant format 8-16 bits integer input supported");
        vsapi->freeNode(d.node);
        vsapi->freeNode(d.edeint);
        return;
    }

    if (!isSameFormat(vsapi->getVideoInfo(d.edeint), &d.vi)) {
        vsapi->setError(out, "Yadifmod: edeint clip must have the same dimensions as main clip and be the same format");
        vsapi->freeNode(d.node);
        vsapi->freeNode(d.edeint);
        return;
    }

    if (vsapi->getVideoInfo(d.edeint)->numFrames != d.vi.numFrames * (d.mode & 1 ? 2 : 1)) {
        vsapi->setError(out, "Yadifmod: edeint clip's number of frames doesn't match");
        vsapi->freeNode(d.node);
        vsapi->freeNode(d.edeint);
        return;
    }

    if (d.mode & 1) {
        d.vi.numFrames *= 2;
        d.vi.fpsNum *= 2;
    }

    const int m = vsapi->propNumElements(in, "planes");

    for (int i = 0; i < 3; i++)
        d.process[i] = m <= 0;

    for (int i = 0; i < m; i++) {
        const int n = int64ToIntS(vsapi->propGetInt(in, "planes", i, nullptr));

        if (n < 0 || n >= d.vi.format->numPlanes) {
            vsapi->setError(out, "Yadifmod: plane index out of range");
            vsapi->freeNode(d.node);
            vsapi->freeNode(d.edeint);
            return;
        }

        if (d.process[n]) {
            vsapi->setError(out, "Yadifmod: plane specified twice");
            vsapi->freeNode(d.node);
            vsapi->freeNode(d.edeint);
            return;
        }

        d.process[n] = true;
    }

    data = new YadifmodData;
    *data = d;

    vsapi->createFilter(in, out, "Yadifmod", yadifmodInit, yadifmodGetFrame, yadifmodFree, fmParallel, 0, data, core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.holywu.yadifmod", "yadifmod", "Modification of Fizick's yadif avisynth filter", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Yadifmod", "clip:clip;edeint:clip;order:int;field:int:opt;mode:int:opt;planes:int[]:opt;", yadifmodCreate, nullptr, plugin);
}
