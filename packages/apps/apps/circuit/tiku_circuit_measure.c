/* tiku_circuit_measure.c -- Measure Distance: two points picked on the
 * canvas, kept in scene millimetres, their distance in the chosen unit,
 * and the marks drawn over the view; the drawing itself is not changed.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include <math.h>
#include <stdio.h>

/** @brief Draw a one-pixel line from (@p x, @p y) to (@p ex, @p ey),
 *         clipped to the surface's clip before it is stepped, since its
 *         ends may lie far outside a zoomed view. */
static void line(tiku_surface_t *s, double x, double y, double ex, double ey)
{
    double dx=ex-x, dy=ey-y, lo=0, hi=1;
    double p[]={-dx,dx,-dy,dy};
    double q[]={x-s->clip.x,s->clip.x+s->clip.w-1-x,
                y-s->clip.y,s->clip.y+s->clip.h-1-y};
    if(s->clip.w<=0 || s->clip.h<=0) return;
    for(int i=0;i<4;i++) {
        if(p[i]==0) { if(q[i]<0) return; }
        else { double t=q[i]/p[i]; if(p[i]<0) lo=fmax(lo,t); else hi=fmin(hi,t); }
    }
    if(lo>hi) return;
    ex=x+hi*dx; ey=y+hi*dy; x+=lo*dx; y+=lo*dy;
    int steps=(int)ceil(fmax(fabs(ex-x),fabs(ey-y)));
    for(int i=0;i<=steps;i++) {
        double t=steps ? (double)i/steps : 0;
        tiku_pixel(s,(int)lround(x+(ex-x)*t),(int)lround(y+(ey-y)*t),0xff245dc1);
    }
}

void cv_measure_reset(tiku_circuitview_t *v)
{
    v->measure_points = v->cursor_valid = 0;
    v->style_open = 0;
}

void cv_measure_text(const tiku_circuitview_t *v, char *out, size_t size)
{
    double factor = v->units == 1 ? 1/25.4 : v->units == 2 ? 1000/25.4 : 1;
    const char *unit = v->units == 1 ? "in" : v->units == 2 ? "mil" : "mm";
    if (v->measure_points) {
        double x = v->measure_points == 2 ? v->measure_x[1] : v->cursor_x;
        double y = v->measure_points == 2 ? v->measure_y[1] : v->cursor_y;
        double dx = (x-v->measure_x[0])*factor, dy = (y-v->measure_y[0])*factor;
        snprintf(out,size,"%sDistance %.4f %s | dX %+.4f, dY %+.4f %s (view axes)",
            v->measure_points == 1 ? "Preview: " : "", hypot(dx,dy),unit,dx,dy,unit);
    } else if (v->measure && !v->cursor_valid) {
        snprintf(out,size,"Measure: click two points. Wheel zooms; arrows pan; Escape clears. Units: %s",unit);
    } else if (v->cursor_valid) {
        snprintf(out,size,"X %.4f  Y %.4f %s (view axes: right/down)%s",v->cursor_x*factor,
            v->cursor_y*factor,unit,v->measure ? " | Click first point" : "");
    } else if (size) *out=0;
}

int cv_measure_event(tiku_circuitview_t *v, const tiku_event_t *e)
{
    if (!v->document || !v->measure) return 0;
    if (e->type == TIKU_EVENT_POINTER_MOVE || e->type == TIKU_EVENT_POINTER_DOWN) {
        v->cursor_valid = cv_inside(v->canvas,e->x,e->y) &&
            tiku_circuit_world(&v->view,e->x-v->canvas.x,e->y-v->canvas.y,&v->cursor_x,&v->cursor_y);
        if (v->dirty) *v->dirty=1;
    }
    if (v->measure && v->cursor_valid && e->type == TIKU_EVENT_POINTER_DOWN && e->button == 1) {
        if (v->measure_points == 2) v->measure_points=0;
        int at=v->measure_points++;
        v->measure_x[at]=v->cursor_x; v->measure_y[at]=v->cursor_y;
        v->dialog.focus=-1; v->dragging=0;
        tiku_circuitview_menus(v);
        return 1;
    }
    return 0;
}

void cv_measure_draw(const tiku_circuitview_t *v, tiku_surface_t *s,
                     tiku_rect_t canvas, const tiku_circuit_view_t *view)
{
    if (!v->measure || !v->measure_points) return;
    double x[2],y[2];
    for (int i=0;i<2;i++) {
        x[i]=canvas.x+view->x+view->scale*(i && v->measure_points==1 ? v->cursor_x : v->measure_x[i]);
        y[i]=canvas.y+view->y+view->scale*(i && v->measure_points==1 ? v->cursor_y : v->measure_y[i]);
        if (!isfinite(x[i]) || !isfinite(y[i])) return;
    }
    tiku_rect_t old=s->clip; tiku_clip_set(s,cv_intersect(old,canvas));
    line(s,x[0],y[0],x[1],y[1]);
    for (int i=0;i<2;i++) {
        line(s,x[i]-4,y[i],x[i]+4,y[i]);
        line(s,x[i],y[i]-4,x[i],y[i]+4);
    }
    tiku_clip_set(s,old);
}
