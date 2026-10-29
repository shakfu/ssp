// based on iemguts/properties

#include <pthread.h>
#include "m_pd.h"
#include "g_canvas.h"

typedef struct _cnv_objlist{
    const t_pd *obj;
    struct _cnv_objlist *next;
}t_cnv_objlist;

typedef struct _cnv_canvaslist{
    const t_pd              *parent;
    t_cnv_objlist           *obj;
    struct _cnv_canvaslist  *next;
}t_cnv_canvaslist;

static t_cnv_canvaslist *s_canvaslist = 0;
// prdt: modules create and free objects on other threads; recursive, as a [click]'s bang can create or free one
static pthread_mutex_t s_canvaslist_lock; // made in click_setup()

static t_cnv_canvaslist *findCanvas(const t_pd *parent){
    t_cnv_canvaslist*list = s_canvaslist;
    if(!parent || !list)
        return(0);
    for(list = s_canvaslist; list; list = list->next){
        if(parent == list->parent)
            return(list);
    }
    return(0);
}

static t_cnv_objlist *objectsInCanvas(const t_pd *parent){
    t_cnv_canvaslist *list = findCanvas(parent);
    if(list)
        return(list->obj);
    return(0);
}

static t_cnv_canvaslist *addCanvas(const t_pd *parent){
    t_cnv_canvaslist *list = findCanvas(parent);
    if(!list){
        list = getbytes(sizeof(*list));
        list->parent = parent;
        list->obj = 0;
        list->next = 0;
        if(s_canvaslist == 0) // new list
            s_canvaslist = list;
        else{ // add to the end of existing list
            t_cnv_canvaslist *dummy = s_canvaslist;
            while(dummy->next)
                dummy = dummy->next;
            dummy->next = list;
        }
    }
    return(list);
}

static void removeObjectFromCanvas(const t_pd*parent, const t_pd*obj) {
    t_cnv_canvaslist *p = findCanvas(parent);
    t_cnv_objlist *list = 0, *last = 0, *next = 0;
    if(!p || !obj)return;
    list = p->obj;
    if(!list)
        return;
    while(list && obj!=list->obj) {
        last = list;
        list = list->next;
    }
    if(!list) /* couldn't find this object */
        return;
    next = list->next;
    if(last)
        last->next = next;
    else
        p->obj = next;
    freebytes(list, sizeof(*list));
    list = 0;
    if(!p->obj){ // prdt: the canvas's entry goes with its last object; it leaked with each canvas
        t_cnv_canvaslist **pp = &s_canvaslist;
        while(*pp != p)
            pp = &(*pp)->next;
        *pp = p->next;
        freebytes(p, sizeof(*p));
    }
}

static void removeObjectFromCanvases(const t_pd*obj) {
    pthread_mutex_lock(&s_canvaslist_lock);
    t_cnv_canvaslist*parents=s_canvaslist;
    while(parents){
        t_cnv_canvaslist *next = parents->next; // removeObjectFromCanvas() may free parents
        removeObjectFromCanvas(parents->parent, obj);
        parents = next;
    }
    pthread_mutex_unlock(&s_canvaslist_lock);
}

static void addObjectToCanvas(const t_pd *parent, const t_pd *obj){
    t_cnv_canvaslist *p = addCanvas(parent);
    t_cnv_objlist *list = 0;
    t_cnv_objlist *entry = 0;
    if(!p || !obj)
        return;
    list = p->obj;
    if(list && obj == list->obj)
        return;
    while(list && list->next){
        if(obj == list->obj) // obj already in list
            return;
        list = list->next;
    }
    entry = getbytes(sizeof(*entry)); // at end of list not containing obj yet, so add it
    entry->obj = obj;
    entry->next = 0;
    if(list)
        list->next = entry;
    else
        p->obj = entry;
}

///////////////////////
// start of click_class
///////////////////////

static t_class *click_class;

typedef struct _click{
  t_object  x_obj;
  t_outlet *x_click_bangout;
}t_click;

static void click_free(t_click *x){
  removeObjectFromCanvases((t_pd*)x);
}

static void click_bang(t_click *x){
    outlet_bang(x->x_click_bangout);
}

static void click_click(t_gobj *z, t_canvas *x){
    x = NULL;
    pthread_mutex_lock(&s_canvaslist_lock);
    t_cnv_objlist *objs = objectsInCanvas((t_pd*)z);
    if(objs == NULL) {
        t_atom arg;
        SETFLOAT(&arg, 1);
        pd_typedmess((t_pd *)z, gensym("vis"), 1, &arg);
    }
    while(objs){
        t_canvas* cv = (t_canvas*)objs->obj;
        click_bang((t_click*)cv);
        objs = objs->next;
    }
    pthread_mutex_unlock(&s_canvaslist_lock);
}

static void *click_new(t_floatarg f){
    t_click *x = (t_click *)pd_new(click_class);
    t_int subpatch_mode = f != 0;;
    t_glist *glist = (t_glist *)canvas_getcurrent();
    t_canvas *canvas = (t_canvas*)glist_getcanvas(glist);
    if(!subpatch_mode){
        while(!canvas->gl_env)
            canvas = canvas->gl_owner;
    }
    pthread_mutex_lock(&s_canvaslist_lock);
    addObjectToCanvas((t_pd*)canvas, (t_pd*)x);
    pthread_mutex_unlock(&s_canvaslist_lock);
    x->x_click_bangout = outlet_new((t_object *)x, &s_bang);
    return(x);
}

void click_setup(void){
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s_canvaslist_lock, &attr);
    pthread_mutexattr_destroy(&attr);
    click_class = class_new(gensym("click"), (t_newmethod)click_new,
        (t_method)click_free, sizeof(t_click), CLASS_NOINLET, A_DEFFLOAT, 0);
    class_addmethod(canvas_class, (t_method)click_click, gensym("click"), 0);
}
