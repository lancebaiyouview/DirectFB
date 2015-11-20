/*
   (c) Copyright 2012-2013  DirectFB integrated media GmbH
   (c) Copyright 2001-2013  The world wide DirectFB Open Source Community (directfb.org)
   (c) Copyright 2000-2004  Convergence (integrated media) GmbH

   All rights reserved.

   Written by Denis Oliver Kropp <dok@directfb.org>,
              Andreas Shimokawa <andi@directfb.org>,
              Marek Pikarski <mass@directfb.org>,
              Sven Neumann <neo@directfb.org>,
              Ville Syrjälä <syrjala@sci.fi> and
              Claudio Ciccani <klan@users.sf.net>.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/



#include <config.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <directfb.h>

#include <direct/mem.h>

#include <fusion/shmalloc.h>

#include <core/core.h>
#include <core/surface_pool.h>

#include <misc/conf.h>

#include "egl_primary.h"
#include "egl_system.h"


#include <core/core_system.h>

//#define RASPBERRY_PI
#define EGL_WAYLAND

#ifdef EGL_WAYLAND
#include <wayland-client.h>
#include <wayland-egl.h>
#endif


DFB_CORE_SYSTEM( egl )

/**********************************************************************************************************************/

static EGLData *m_data;    /* FIXME: Fix Core System API to pass data in all functions. */

/**********************************************************************************************************************/

#if defined EGL_WAYLAND
struct geometry {
     int width, height;
};

struct wayland_context
{
     struct wl_display *display;
     struct wl_surface *surface;
     struct wl_compositor *compositor;
     struct wl_shell *shell;
     struct wl_shell_surface *shell_surface;
     struct geometry window_size;
     struct wl_egl_window *native;
};

static void *wl_event_loop(void *arg)
{
     struct wayland_context *context = arg;
     wl_display_flush(context->display);
     while (1)
     {
          wl_display_dispatch(context->display);
     }
     return NULL;
}

static void handle_shell_surface_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
     wl_shell_surface_pong(shell_surface, serial);
}

static void handle_shell_surface_configure(void *data, struct wl_shell_surface *shell_surface, uint32_t edges, int32_t width, int32_t height)
{
}

static void handle_shell_surface_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener =
{
     handle_shell_surface_ping,
     handle_shell_surface_configure,
     handle_shell_surface_popup_done
};

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
     struct wayland_context *d = data;

     if (strcmp(interface, "wl_compositor") == 0) {
          d->compositor =
               wl_registry_bind(registry, name,
                         &wl_compositor_interface, 1);
     } else if (strcmp(interface, "wl_shell") == 0) {
          d->shell = wl_registry_bind(registry, name,
                    &wl_shell_interface, 1);

     }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry,
          uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
     registry_handle_global,
     registry_handle_global_remove
};
#endif

static DFBResult
InitEGL( EGLData *egl )
{
     int    err;
     EGLint iMajorVersion, iMinorVersion;
     EGLint ai32ContextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

#ifdef RASPBERRY_PI
     static EGL_DISPMANX_WINDOW_T *nativewindow = malloc(sizeof(EGL_DISPMANX_WINDOW_T));;
     DISPMANX_ELEMENT_HANDLE_T dispman_element;
     DISPMANX_DISPLAY_HANDLE_T dispman_display;
     DISPMANX_UPDATE_HANDLE_T dispman_update;
     VC_RECT_T dst_rect;
     VC_RECT_T src_rect;

     bcm_host_init();

#endif     

#if defined EGL_WAYLAND
     struct wayland_context *context = calloc(1, sizeof(*context));

     context->window_size.width  = 1920;
     context->window_size.height = 1080;

     context->display = wl_display_connect(NULL);
     struct wl_registry *registry = wl_display_get_registry(context->display);
     wl_registry_add_listener(registry, &registry_listener, context);
     wl_display_dispatch(context->display);
     wl_display_roundtrip(context->display);

     egl->eglDisplay = eglGetDisplay(context->display);

     context->surface = wl_compositor_create_surface(context->compositor);

     context->shell_surface = wl_shell_get_shell_surface(context->shell, context->surface);
     wl_shell_surface_add_listener(context->shell_surface, &shell_surface_listener, context);
     wl_shell_surface_set_fullscreen(context->shell_surface, WL_SHELL_SURFACE_FULLSCREEN_METHOD_DEFAULT, 60000, NULL);

#endif
#if !defined EGL_WAYLAND
     egl->eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif

     if (!eglInitialize(egl->eglDisplay, &iMajorVersion, &iMinorVersion))
          return DFB_INIT;

     egl->eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
     if (egl->eglCreateImageKHR == NULL) {
          D_ERROR( "DirectFB/EGL: eglCreateImageKHR not found!\n" );
          return DFB_UNSUPPORTED;
     }

     egl->glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
     if (egl->glEGLImageTargetTexture2DOES == NULL) {
          D_ERROR( "DirectFB/EGL: glEGLImageTargetTexture2DOES not found!\n" );
          return DFB_UNSUPPORTED;
     }


     eglBindAPI(EGL_OPENGL_ES_API);
     if (!TestEGLError("eglBindAPI"))
          return DFB_INIT;

     EGLint pi32ConfigAttribs[] = {  EGL_RED_SIZE, 8,
                                     EGL_GREEN_SIZE, 8,
                                     EGL_BLUE_SIZE, 8,
                                     EGL_ALPHA_SIZE, 8,
                                     EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                     EGL_NONE
                                  };

     int iConfigs;
     if (!eglChooseConfig(egl->eglDisplay, pi32ConfigAttribs, &egl->eglConfig, 1, &iConfigs) || (iConfigs != 1)) {
          D_ERROR("DirectFB/EGL: eglChooseConfig() failed.\n");
          return DFB_INIT;
     }
     
     egl->eglContext = eglCreateContext(egl->eglDisplay, egl->eglConfig, EGL_NO_CONTEXT, ai32ContextAttribs);
     if (!TestEGLError("eglCreateContext"))
          return DFB_INIT;
     
#ifdef RASPBERRY_PI
     graphics_get_display_size(0 /* LCD */, &egl->DisplayWidth, &egl->DisplayHeight);

     dst_rect.x = 0;
     dst_rect.y = 0;
     dst_rect.width = egl->DisplayWidth;
     dst_rect.height = egl->DisplayHeight;

     src_rect.x = 0;
     src_rect.y = 0;
     src_rect.width  = dst_rect.width  << 16; // ANDI: fixed point 16.16?
     src_rect.height = dst_rect.height << 16;

     dispman_display = vc_dispmanx_display_open( 0);
     dispman_update = vc_dispmanx_update_start( 0 );

     dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,  0, &dst_rect, 0,
                                                 &src_rect, DISPMANX_PROTECTION_NONE, 0, 0, 0);

     nativewindow->element = dispman_element;
     nativewindow->width   = egl->DisplayWidth;
     nativewindow->height  = egl->DisplayHeight;
     vc_dispmanx_update_submit_sync( dispman_update );

#endif
#ifdef EGL_WAYLAND
     context->native = wl_egl_window_create(context->surface, context->window_size.width, context->window_size.height);
     void *nativewindow = context->native;
#endif

     egl->eglSurface = eglCreateWindowSurface( egl->eglDisplay, egl->eglConfig, nativewindow, NULL );
     if (!TestEGLError("eglCreateWindowSurface"))
          return DFB_INIT;


     eglMakeCurrent( egl->eglDisplay, egl->eglSurface, egl->eglSurface, egl->eglContext );
     if (!TestEGLError("eglMakeCurrent"))
          return DFB_INIT;

     eglSwapInterval( egl->eglDisplay, 1 );
     if (!TestEGLError("eglSwapInterval"))
          return DFB_INIT;

     if ((err = glGetError()) != 0) {
          D_ERROR( "DirectFB/EGL: Error at end of InitEGL! (error = %x)\n", err );
          //return DFB_FAILURE;
     }

#ifdef EGL_WAYLAND
     wl_display_roundtrip(context->display);
     pthread_t pt;
     pthread_create(&pt, NULL, wl_event_loop, context);
#endif

     return DFB_OK;
}

/**********************************************************************************************************************/

static void
system_get_info( CoreSystemInfo *info )
{
     info->type = CORE_EGL;
     info->caps = CSCAPS_ACCELERATION;

     snprintf( info->name, DFB_CORE_SYSTEM_INFO_NAME_LENGTH, "EGL" );
}

static DFBResult
system_initialize( CoreDFB *core, void **ret_data )
{
     DFBResult            ret;
     EGLData            *data;
     EGLDataShared      *shared;
     FusionSHMPoolShared *pool;

     D_ASSERT( m_data == NULL );

     data = D_CALLOC( 1, sizeof(EGLData) );
     if (!data)
          return D_OOM();

     data->core = core;

     pool = dfb_core_shmpool( core );

     shared = SHCALLOC( pool, 1, sizeof(EGLDataShared) );
     if (!shared) {
          D_FREE( data );
          return D_OOSHM();
     }

     shared->shmpool = pool;

     data->shared = shared;

     ret = InitEGL( data );
     if (ret) {
          SHFREE( pool, shared );
          D_FREE( data );
          return ret;
     }

     *ret_data = m_data = data;

     data->screen = dfb_screens_register( NULL, data, eglPrimaryScreenFuncs );
     data->layer  = dfb_layers_register( data->screen, data, eglPrimaryLayerFuncs );

     dfb_surface_pool_initialize( core, eglSurfacePoolFuncs, &shared->pool );

     core_arena_add_shared_field( core, "egl", shared );

     return DFB_OK;
}

static DFBResult
system_join( CoreDFB *core, void **ret_data )
{
     DFBResult         ret;
     void             *tmp;
     EGLData       *data;
     EGLDataShared *shared;

     D_ASSERT( m_data == NULL );

     data = D_CALLOC( 1, sizeof(EGLData) );
     if (!data)
          return D_OOM();

     data->core = core;

     ret = core_arena_get_shared_field( core, "egl", &tmp );
     if (ret) {
          D_FREE( data );
          return ret;
     }

     data->shared = shared = tmp;


     *ret_data = m_data = data;

     data->screen = dfb_screens_register( NULL, data, eglPrimaryScreenFuncs );
     data->layer  = dfb_layers_register( data->screen, data, eglPrimaryLayerFuncs );

     dfb_surface_pool_join( core, shared->pool, eglSurfacePoolFuncs );

     return DFB_OK;
}

static DFBResult
system_shutdown( bool emergency )
{
     EGLDataShared *shared;

     D_ASSERT( m_data != NULL );

     shared = m_data->shared;
     D_ASSERT( shared != NULL );

     dfb_surface_pool_destroy( shared->pool );


     SHFREE( shared->shmpool, shared );

     D_FREE( m_data );
     m_data = NULL;

     return DFB_OK;
}

static DFBResult
system_leave( bool emergency )
{
     EGLDataShared *shared;

     D_ASSERT( m_data != NULL );

     shared = m_data->shared;
     D_ASSERT( shared != NULL );

     dfb_surface_pool_leave( shared->pool );


     D_FREE( m_data );
     m_data = NULL;

     return DFB_OK;
}

static DFBResult
system_suspend()
{
     D_ASSERT( m_data != NULL );

     return DFB_OK;
}

static DFBResult
system_resume()
{
     D_ASSERT( m_data != NULL );

     return DFB_OK;
}

static volatile void *
system_map_mmio( unsigned int    offset,
                 int             length )
{
     return NULL;
}

static void
system_unmap_mmio( volatile void  *addr,
                   int             length )
{
}

static int
system_get_accelerator()
{
     return dfb_config->accelerator;
}

static VideoMode *
system_get_modes()
{
     return NULL;
}

static VideoMode *
system_get_current_mode()
{
     return NULL;
}

static DFBResult
system_thread_init()
{
     return DFB_OK;
}

static bool
system_input_filter( CoreInputDevice *device,
                     DFBInputEvent   *event )
{
     return false;
}

static unsigned long
system_video_memory_physical( unsigned int offset )
{
     return 0;
}

static void *
system_video_memory_virtual( unsigned int offset )
{
     return NULL;
}

static unsigned int
system_videoram_length()
{
     return 0;
}

static unsigned long
system_aux_memory_physical( unsigned int offset )
{
     return 0;
}

static void *
system_aux_memory_virtual( unsigned int offset )
{
     return NULL;
}

static unsigned int
system_auxram_length()
{
     return 0;
}

static void
system_get_busid( int *ret_bus, int *ret_dev, int *ret_func )
{
     return;
}

static void
system_get_deviceid( unsigned int *ret_vendor_id,
                     unsigned int *ret_device_id )
{
     return;
}

static int
system_surface_data_size( void )
{
     /* Return zero because shared surface data is unneeded. */
     return 0;
}

static void
system_surface_data_init( CoreSurface *surface, void *data )
{
     /* Ignore since unneeded. */
}

static void
system_surface_data_destroy( CoreSurface *surface, void *data )
{
     /* Ignore since unneeded. */
}

