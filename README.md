# Assignment from ContextVision

A simple media player and adjustable filter created in Visual Studio 2017 using the GStreamer library.
## Getting started
The mediaplayer folder contains the media player solution, while the blurfilter and bilateralfilter folders contain the filter plugin solutions.

The media folder contains a short example video used in this example.
### Prerequisite
Visual Studio was used to create this application, and for the sake of simplicity the VS solutions are uploaded to this repository for necessary library linking.

[GStreamer for windows](https://gstreamer.freedesktop.org/documentation/installing/on-windows.html) is required to build these applications, both the runtime files and the development files.

An Environment Variable named GSTREAMER_1_0_ROOT_X86_64 set to the folder where GStreamer is installed is necessary as well.
### To Run
First, the blur filter must be built and the resulting files libgstblurfilter.dll and libgstblurfilter.lib copied to the gstreamer 1.0 library folder located at $(GSTREAMER_1_0_ROOT_X86_64)\lib\gstreamer-1.0 

Second, in mediaplayer\mediaplayer.cpp at line 79, the URI address to the video needs to be updated to where the repository is located. Also, the mediaplayer solution requires its working directory to be $(GSTREAMER_1_0_ROOT_X86_64)\bin to ensure necessary dll libraries.

After that, all properties *should* be set correctly to build and run the application. If not, adding the property sheets gstreamer-1.0.props for the media player and gstreamer-1.0.props, gstreamer-base-1.0.props and gstreamer-pbutils-1.0 for the filter, all located at $(GSTREAMER_1_0_ROOT_X86_64)\share\vs\2010\libs, should solve the problem.

To use the bilateral filter, the same steps as for the blur filter must be taken. One must also tell the mediaplayer to use the bilateral filter, which is done by replacing the two instances of "blurfilter" at line 46 in mediaplayer\mediaplayer.cpp to "bilateralfilter".
