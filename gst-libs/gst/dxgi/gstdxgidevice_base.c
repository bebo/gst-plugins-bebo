/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdxgidevice_base.h"
#include <gst/dxgi/gstdxgidevice_d3d12.h>

G_DEFINE_TYPE (GstDXGIDevice, gst_dxgi_device, GST_TYPE_OBJECT);

static void
gst_dxgi_device_class_init (GstDXGIDeviceClass * klass)
{
}

static void
gst_dxgi_device_init (GstDXGIDevice * device)
{
}

GstDXGIDevice *
gst_dxgi_device_new ()
{
  return NULL;
}

