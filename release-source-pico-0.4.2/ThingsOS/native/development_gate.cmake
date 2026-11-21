# USB Serial/JTAG and ROM download stay enabled during normal reader operation.
# Standard IDF service targets are available by default. For a fresh installation
# use package_usb.py's complete image (boot guard, rescue, OTA selector and font).
# Packaging and building never connect to or write a device.
idf_build_get_property(thingsos_usb_python PYTHON)
add_custom_target(usb-bundle
 COMMAND ${thingsos_usb_python} "${CMAKE_CURRENT_LIST_DIR}/package_usb.py"
 WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
 VERBATIM)
