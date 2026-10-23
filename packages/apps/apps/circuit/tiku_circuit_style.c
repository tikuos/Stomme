/* tiku_circuit_style.c -- Layer Appearance...: a panel over the window
 * that sets the selected layer's colour and opacity, by hex value or
 * palette, and by number or slider.
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * SPDX-License-Identifier: Apache-2.0 */
#include "tiku_circuit_window_private.h"
#include "tiku_colors_paint.h"
#include "tiku_slider_paint.h"
#include "tiku_text_layout_paint.h"
#include "tiku_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* The panel's control ids, above the window's command numbers. */
enum { COLOUR=1001, OPACITY, PALETTE, SLIDER, APPLY, DISMISS };

/** @brief Lay the panel out for the window as it is now, its controls in
 *         tab order. */
static void layout(tiku_circuitview_t *v)
{
    tiku_rect_t r=tiku_window_content(v->window);
    int w=r.w-16; if(w>420) w=420;
    int bh=tiku_ui_button_h(), h=8*bh+56;
    v->style_rect=(tiku_rect_t){r.x+(r.w-w)/2,r.y+8,w,h};
    int x=v->style_rect.x+12,y=v->style_rect.y+bh+10;
    tiku_dialog_begin(&v->style_dialog,APPLY,DISMISS);
    tiku_dialog_add(&v->style_dialog,COLOUR,(tiku_rect_t){x+96,y,w-120,bh},TIKU_DIALOG_INPUT,1);
    y+=bh+8;
    v->palette_rect=(tiku_rect_t){x,y,w-24,2*bh};
    tiku_dialog_add(&v->style_dialog,PALETTE,v->palette_rect,TIKU_DIALOG_INPUT,1);
    y+=2*bh+8;
    tiku_dialog_add(&v->style_dialog,OPACITY,(tiku_rect_t){x+96,y,70,bh},TIKU_DIALOG_INPUT,1);
    y+=bh+8;
    v->opacity_rect=(tiku_rect_t){x,y,w-24,bh};
    tiku_dialog_add(&v->style_dialog,SLIDER,v->opacity_rect,TIKU_DIALOG_INPUT,1);
    y+=2*bh+8;
    int button=tiku_ui_button_w("Cancel",70);
    tiku_dialog_add(&v->style_dialog,DISMISS,(tiku_rect_t){x,y,button,bh},TIKU_DIALOG_BUTTON,1);
    tiku_dialog_add(&v->style_dialog,APPLY,(tiku_rect_t){v->style_rect.x+w-button-12,y,button,bh},TIKU_DIALOG_BUTTON,1);
    tiku_dialog_end(&v->style_dialog);
}

void cv_style_open(tiku_circuitview_t *v)
{
    const tiku_circuit_layer_t *l=tiku_circuit_layer(v->document,v->sheet);
    if (!l) return;
    v->style_open=1; v->style_layer=v->sheet; v->style_generation=v->load_generation;
    v->style_error[0]=0;
    tiku_dialog_init(&v->style_dialog);
    tiku_colors_init(&v->palette,8);
    for(int i=0;i<8;i++) tiku_colors_add(&v->palette,tiku_circuit_layer_colour((size_t)i));
    tiku_colors_select_nearest(&v->palette,l->colour);
    char text[32]; snprintf(text,sizeof text,"#%06X",l->colour&0xffffff);
    tiku_field_start(&v->colour_text,text); tiku_field_input_init(&v->colour_input);
    tiku_slider_init(&v->opacity,0,100,(int)(l->opacity*100+127)/255,1);
    snprintf(text,sizeof text,"%d",v->opacity.value);
    tiku_field_start(&v->opacity_text,text); tiku_field_input_init(&v->opacity_input);
    layout(v); tiku_dialog_focus(&v->style_dialog,COLOUR);
    if(v->dirty) *v->dirty=1;
    tiku_circuitview_menus(v);
}

void cv_style_draw(tiku_circuitview_t *v,tiku_surface_t *s)
{
    if(!v->style_open) return;
    layout(v);
    tiku_fill(s,v->style_rect,TIKU_C_PANEL); tiku_frame(s,v->style_rect,TIKU_C_TEXT);
    tiku_rect_t title=tiku_inset(v->style_rect,12); title.h=tiku_ui_button_h();
    tiku_text_line_draw(s,title,0,"Layer appearance",TIKU_C_TEXT);
    tiku_rect_t content=tiku_window_content(v->window);
    if(content.w<300 || content.h<v->style_rect.h+16) {
        title.y+=title.h; title.h=content.h-title.h-24;
        tiku_text_block_draw(s,title,0,"Enlarge this window to edit layer appearance. Escape closes this panel.",TIKU_C_TEXT,2);
        return;
    }
    for(int i=0;i<v->style_dialog.count;i++) {
        tiku_dialog_control_t *c=&v->style_dialog.control[i];
        unsigned flags=tiku_dialog_focused(&v->style_dialog,c->id)?TIKU_S_FOCUS:0;
        if(tiku_dialog_pressed(&v->style_dialog,c->id)) flags|=TIKU_S_PRESSED;
        if(c->id==COLOUR || c->id==OPACITY) {
            tiku_field_t *f=c->id==COLOUR?&v->colour_text:&v->opacity_text;
            tiku_field_input_t *input=c->id==COLOUR?&v->colour_input:&v->opacity_input;
            tiku_ui_textfield_scroll(s,c->rect,f->text,flags?f->caret:-1,flags?f->sel_a:0,flags?f->sel_b:0,flags,input->scroll);
            tiku_text_line_draw(s,(tiku_rect_t){title.x,c->rect.y,92,c->rect.h},0,c->id==COLOUR?"Colour (hex)":"Opacity (%)",TIKU_C_TEXT);
        } else if(c->id==PALETTE) tiku_colors_draw(&v->palette,s,c->rect);
        else if(c->id==SLIDER) tiku_slider_draw(&v->opacity,s,c->rect);
        else tiku_ui_button(s,c->rect,c->id==APPLY?"Apply":"Cancel",flags);
    }
    tiku_text_line_draw(s,(tiku_rect_t){title.x,v->opacity_rect.y+v->opacity_rect.h+4,title.w,tiku_ui_button_h()},0,v->style_error,0xff993322);
}

/** @brief Write the slider's value into the opacity field. */
static void opacity_text(tiku_circuitview_t *v)
{ char s[16]; snprintf(s,sizeof s,"%d",v->opacity.value); tiku_field_start(&v->opacity_text,s); }

int cv_style_event(tiku_circuitview_t *v,const tiku_event_t *e)
{
    if(!v->style_open) return 0;
    layout(v);
    tiku_rect_t content=tiku_window_content(v->window);
    if(content.w<300 || content.h<v->style_rect.h+16) {
        if(e->type==TIKU_EVENT_KEY_DOWN && e->key==TIKU_KEY_ESCAPE) {
            v->style_open=0; tiku_circuitview_menus(v); if(v->dirty) *v->dirty=1;
        }
        return 1;
    }
    if(e->type==TIKU_EVENT_DEACTIVATED) tiku_slider_release(&v->opacity);
    if(e->type==TIKU_EVENT_POINTER_DOWN) for(int i=0;i<v->style_dialog.count;i++)
        if(cv_inside(v->style_dialog.control[i].rect,e->x,e->y)) tiku_dialog_focus(&v->style_dialog,v->style_dialog.control[i].id);
    if(e->type==TIKU_EVENT_POINTER_DOWN && e->button==1 && cv_inside(v->opacity_rect,e->x,e->y)) {
        tiku_slider_press(&v->opacity,v->opacity_rect,e->x); opacity_text(v);
    } else if(e->type==TIKU_EVENT_POINTER_MOVE && v->opacity.tracking) {
        tiku_slider_drag(&v->opacity,v->opacity_rect,e->x); opacity_text(v);
    } else if(e->type==TIKU_EVENT_POINTER_UP) tiku_slider_release(&v->opacity);
    int palette=tiku_colors_hit(&v->palette,v->palette_rect,e->x,e->y);
    int key=e->type==TIKU_EVENT_KEY_DOWN;
    if(key && v->style_dialog.focus==PALETTE && (e->key==TIKU_KEY_LEFT || e->key==TIKU_KEY_RIGHT))
        palette=v->palette.current+(e->key==TIKU_KEY_LEFT?-1:1);
    if((e->type==TIKU_EVENT_POINTER_DOWN && e->button==1 && palette>=0) ||
       (key && v->style_dialog.focus==PALETTE && (e->key==TIKU_KEY_LEFT || e->key==TIKU_KEY_RIGHT))) {
        tiku_colors_select(&v->palette,palette); char s[16];
        snprintf(s,sizeof s,"#%06X",tiku_colors_value(&v->palette)&0xffffff);
        tiku_field_start(&v->colour_text,s);
    }
    if(key && v->style_dialog.focus==SLIDER && (e->key==TIKU_KEY_LEFT || e->key==TIKU_KEY_RIGHT)) {
        tiku_slider_step(&v->opacity,e->key==TIKU_KEY_LEFT?-1:1); opacity_text(v);
    }
    int command=0, focus=v->style_dialog.focus;
    if((focus==COLOUR || focus==OPACITY) && !(key&&(e->key==TIKU_KEY_TAB || e->key==TIKU_KEY_ESCAPE))) {
        tiku_rect_t r={0}; for(int i=0;i<v->style_dialog.count;i++) if(v->style_dialog.control[i].id==focus) r=v->style_dialog.control[i].rect;
        tiku_field_result_t result=tiku_field_event(focus==COLOUR?&v->colour_text:&v->opacity_text,
            focus==COLOUR?&v->colour_input:&v->opacity_input,e,r,1);
        if(result==TIKU_FIELD_COMMITTED) command=APPLY;
        if(focus==OPACITY) {
            const char *text=v->opacity_text.text;
            if(*text && strlen(text)<=3 && strspn(text,"0123456789")==strlen(text) && atoi(text)<=100)
                tiku_slider_set(&v->opacity,atoi(text));
        }
    }
    tiku_dialog_result_t result=tiku_dialog_event(&v->style_dialog,e);
    if(result.activated>=0) command=result.activated;
    if(command==DISMISS) v->style_open=0;
    if(command==APPLY) {
        const char *s=v->colour_text.text; if(*s=='#') s++;
        char *end; unsigned long colour=strtoul(s,&end,16);
        int valid=strlen(s)==6 && !*end && strspn(s,"0123456789abcdefABCDEF")==6;
        const char *op=v->opacity_text.text; unsigned long opacity=strtoul(op,&end,10);
        valid=valid && *op && !*end && strspn(op,"0123456789")==strlen(op) && opacity<=100;
        if(!valid) snprintf(v->style_error,sizeof v->style_error,"Use six hex digits and opacity 0-100.");
        else if(v->style_generation!=v->load_generation || v->style_layer!=v->sheet) {
            v->style_open=0; tiku_circuitview_note(v,"The layer changed; reopen Layer Appearance.");
        } else if(cv_layer_style(v,(uint32_t)colour,(unsigned)(opacity*255+50)/100)) v->style_open=0;
    }
    if(v->dirty) *v->dirty=1;
    tiku_circuitview_menus(v);
    return 1;
}
