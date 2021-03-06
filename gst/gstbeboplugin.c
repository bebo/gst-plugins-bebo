/* GStreamer NVENC plugin
 * Copyright (C) 2015 Centricular Ltd
 * Copyright (C) 2018 Pigs In Flight, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "dshowfiltersink/gstdshowsink.h"
#include "nvenc/gstnvh264enc.h"
#include "nvenc/gstnvenc.h"
#include "gl2dxgi/gstgl2dxgi.h"
#include "bufferholder/gstbufferholder.h"
#include "noisegate/gstaudionoisegate.h"
#include "noisesuppression/gstaudionoisesuppression.h"

// Note: This is to prefer discrete gpu rather than integrated gpu.
// This is increase performance with gl/dxgi. Applications required
// to be signed otherwise these flags won't be doing anything.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (load_nvenc_dlls()) {
    gst_element_register(plugin, "d3dnvh264enc", GST_RANK_PRIMARY * 2,
        gst_nv_h264_enc_get_type());
  }
  if (!gst_element_register(plugin, "dshowfiltersink",
    GST_RANK_NONE, GST_TYPE_SHM_SINK)) {
    return FALSE;
  }
  if (!gst_element_register(plugin, "gl2dxgi",
    GST_RANK_NONE, GST_TYPE_GL_2_DXGI)) {
    return FALSE;
  }
  if (!gst_element_register(plugin, "noisegate",
    GST_RANK_NONE, GST_TYPE_AUDIO_NOISE_GATE)) {
    return FALSE;
  }
  if (!gst_element_register(plugin, "noisesuppression",
    GST_RANK_NONE, GST_TYPE_AUDIO_NOISE_SUPPRESSION)) {
    return FALSE;
  }
  if (!gst_element_register(plugin, "bufferholder",
    GST_RANK_NONE, GST_TYPE_BUFFER_HOLDER)) {
    return FALSE;
  }
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    bebo,
    "Bebo GStreamer plugin",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
