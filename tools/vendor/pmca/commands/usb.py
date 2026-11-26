"""Minimal USB driver enumeration helpers extracted from ma1co/Sony-PMCA-RE (MIT).

Original project: https://github.com/ma1co/Sony-PMCA-RE
Copyright (c) 2015 ma1co (MIT License - see ../LICENSE-MIT.txt)

Contains only UsbDriverList, importDriver, listDevices, and getDevice needed for
Sony MSC / MTP / Senser Service Mode detection without pulling in APK/appstore/firmware modules.
"""

import contextlib
import sys

from ..usb import *
from ..usb.sony import *


class UsbDriverList(contextlib.AbstractContextManager):
 def __init__(self, *contexts):
  self._contexts = contexts
  self._drivers = []

 def __enter__(self):
  self._drivers = [context.__enter__() for context in self._contexts]
  return self

 def __exit__(self, *ex):
  for context in self._contexts:
   context.__exit__(*ex)
  self._drivers = []

 def listDevices(self, vendor):
  for driver in self._drivers:
   for dev in driver.listDevices(vendor):
    yield dev, driver.classType, driver.openDevice(dev)


def importDriver(driverName=None):
 """Imports the usb driver. Use in a with statement"""
 MscContext = None
 MtpContext = None
 VendorSpecificContext = None
 MscContext2 = None
 MtpContext2 = None
 VendorSpecificContext2 = None

 # Load native drivers (optional; libusb fallback is always included below)
 if driverName == 'native' or driverName is None:
  if sys.platform == 'win32':
   try:
    from ..usb.driver.windows.msc import MscContext
   except ImportError:
    MscContext = None
  elif sys.platform == 'darwin':
   try:
    from ..usb.driver.osx import isMscDriverAvailable
    if isMscDriverAvailable():
     from ..usb.driver.osx import MscContext
    else:
     print('Native driver not installed')
   except ImportError:
    pass
  else:
   print('No native drivers available')
 elif driverName != 'libusb':
  raise Exception('Unknown driver')

 # Fallback to libusb
 if MscContext is None or (driverName is None and sys.platform == 'win32'):
  from ..usb.driver.generic.libusb import MscContext as MscContext2
 if MtpContext is None or (driverName is None and sys.platform == 'win32'):
  from ..usb.driver.generic.libusb import MtpContext as MtpContext2
 if VendorSpecificContext is None or (driverName is None and sys.platform == 'win32'):
  from ..usb.driver.generic.libusb import VendorSpecificContext as VendorSpecificContext2

 drivers = [context() for context in [MscContext, MtpContext, VendorSpecificContext, MscContext2, MtpContext2, VendorSpecificContext2] if context]
 print('Using drivers %s' % ', '.join(d.name for d in drivers))
 return UsbDriverList(*drivers)


def listDevices(driverList, quiet=False):
 """List all Sony usb devices"""
 if not quiet:
  print('Looking for Sony devices')
 for dev, type, drv in driverList.listDevices(SONY_ID_VENDOR):
  if type == USB_CLASS_MSC:
   if not quiet:
    print('\nQuerying mass storage device')
   info = MscDevice(drv).getDeviceInfo()

   if isSonyMscCamera(info):
    if isSonyMscUpdaterCamera(dev):
     if not quiet:
      print('%s %s is a camera in updater mode' % (info.manufacturer, info.model))
     yield SonyMscUpdaterDevice(drv)
    else:
     if not quiet:
      print('%s %s is a camera in mass storage mode' % (info.manufacturer, info.model))
     yield SonyMscExtCmdDevice(drv)

  elif type == USB_CLASS_PTP:
   if not quiet:
    print('\nQuerying MTP device')
   info = MtpDevice(drv).getDeviceInfo()

   if isSonyMtpCamera(info):
    if not quiet:
     print('%s %s is a camera in MTP mode' % (info.manufacturer, info.model))
    yield SonyMtpExtCmdDevice(drv)
   elif isSonyMtpAppInstallCamera(info):
    if not quiet:
     print('%s %s is a camera in app install mode' % (info.manufacturer, info.model))
    yield SonyMtpAppInstallDevice(drv)

  elif type == USB_CLASS_VENDOR_SPECIFIC:
   if isSonySenserCamera(dev):
    print('Found a camera in service mode')
    yield SonySenserDevice(drv)

  if not quiet:
   print('')


def getDevice(driver):
 """Check for exactly one Sony usb device"""
 devices = list(listDevices(driver))
 if not devices:
  print('No devices found. Please make sure that the camera is connected.')
 elif len(devices) != 1:
  print('Error: Too many Sony devices found. Only one camera is supported.')
 else:
  return devices[0]
