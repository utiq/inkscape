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
    mergetype mt = mt_levien;
    auto len = data.size();

// std::cout << "points: " << len << " -----------------\n";
    // for (int i = 0; i < len; ++i) {
// std::cout << ' ' << data[i].x() << ',' << data[i].y();
    // }
// std::cout << std::endl;

    std::vector<FitPoint> fit;
    for (int i = 0; i < len; ++i) {
        fit.push_back({});
        auto& fp = fit.back();
        fp.p.x = data[i].x();
        fp.p.y = data[i].y();
        fp.t = data[i].t;
        fp.ut.x = fp.ut.y = 0;
    }

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
            // sp->pointtype = pt_curve;
        }

        if (i == 0) {
            input->first = input->last = sp; 
        }
        else {
            SplineMake(input->last, sp, order2);
            input->last = sp;
        }
    }

    // if (input->last->prev) input->last->prev->knownlinear = false;
// std::cout << "\nlastp: " << input->last->me.x << ',' << input->last->me.y << '\n';

    Spline* spline = ApproximateSplineFromPointsSlopes(input->first, input->last, fit.data(), fit.size(), order2, mt);
    // Spline* spline = ApproximateSplineFromPoints(input->first, input->last, mid, cnt, order2);

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
// std::cout << "spline\n";
// std::cout << "start: " << bezier[0].x() << ',' << bezier[0].y() << " - end: " << bezier[3].x() << ',' << bezier[3].y() << std::endl;
// std::cout << "cp1: " << bezier[1].x() << ',' << bezier[1].y() << " - cp2: " << bezier[2].x() << ',' << bezier[2].y() << std::endl;

// std::cout << "all cpt spline\n";
// std::cout << "ctlp1: " << spline->from->prevcp.x << ',' << spline->from->prevcp.y << " - " << spline->from->nextcp.x << ',' <<  spline->from->nextcp.y << std::endl;
// std::cout << "ctlp2: " << spline->to->prevcp.x << ',' << spline->to->prevcp.y << " - " << spline->to->nextcp.x << ',' <<  spline->to->nextcp.y << std::endl;
    SplinePointListFree(input);
    // SplineFree(spline);
    return spline != nullptr;
}
