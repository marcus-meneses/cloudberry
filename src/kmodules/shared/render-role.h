#ifndef CLOUDBERRY_RENDER_ROLE_H
#define CLOUDBERRY_RENDER_ROLE_H
/* Input is an already catalog-authenticated provider installation basename.
 * Public handle names and kernel authorization roles are backend-independent. */
static inline int render_name_equal(const char *a,const char *b){while(*a&&*a==*b){a++;b++;}return *a==*b;}
static inline const char *render_provider_role(const char *name){
    if(render_name_equal(name,"display-vga")||render_name_equal(name,"display-opengl"))return "display";
    if(render_name_equal(name,"graphics-vga")||render_name_equal(name,"graphics-opengl"))return "graphics";
    return name;
}
#endif
