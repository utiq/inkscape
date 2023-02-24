// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <vector>
#include "bezier-fit.h"

extern "C" {
    #include "splinefit.h"
    #include "splinefont.h"
}

int bezier_fit(Geom::Point bezier[4], const std::vector<InputPoint>& data) {

    if (data.size() <= 2) return 0;

    int order2 = false; // not 2nd order, so cubic
    // "Fitting cubic Bézier curves"
    // https://raphlinus.github.io/curves/2021/03/11/bezier-fitting.html
    mergetype mt = mt_levien;
    auto len = data.size();

    std::vector<FitPoint> fit;
    for (int i = 0; i < len; ++i) {
        fit.push_back({});
        auto& fp = fit.back();
        fp.p.x = data[i].x();
        fp.p.y = data[i].y();
        fp.t = data[i].t;
        fp.ut.x = fp.ut.y = 0;
    }

    // transform data into spline set format

	auto input = (SplineSet*)chunkalloc(sizeof(SplineSet));

    for (int i = 0; i < len; ++i) {
        auto& d = data[i];
		auto sp = SplinePointCreate(d.x(), d.y());
        if (d.have_slope) {
            sp->nextcp.x = d.front.x();
            sp->nextcp.y = d.front.y();
            sp->nonextcp = false;
            sp->prevcp.x = d.back.x();
            sp->prevcp.y = d.back.y();
            sp->noprevcp = false;
        }

        if (i == 0) {
            input->first = input->last = sp; 
        }
        else {
            SplineMake(input->last, sp, order2);
            input->last = sp;
        }
    }

    Spline* spline = ApproximateSplineFromPointsSlopes(input->first, input->last, fit.data(), fit.size(), order2, mt);

    if (spline) {
        bezier[0].x() = spline->from->me.x;
        bezier[0].y() = spline->from->me.y;

        bezier[1].x() = spline->from->nextcp.x;
        bezier[1].y() = spline->from->nextcp.y;

        bezier[2].x() = spline->to->prevcp.x;
        bezier[2].y() = spline->to->prevcp.y;

        bezier[3].x() = spline->to->me.x;
        bezier[3].y() = spline->to->me.y;
    }
    SplinePointListFree(input);
    //TODO: verify that all C structs are freed up
    // SplineFree(spline);
    return spline != nullptr;
}
