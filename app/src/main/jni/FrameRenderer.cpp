//
// Created by Eric on 12/31/2015.
//

FrameRenderer::FrameRenderer(JNIEnv* jenv, jstring rsPath, DeviceSettings dSets) {

	int bufLength;  // number of bytes in buffer, depends on color space

	frameWidth = dSets.frame_width;
	frameHeight = dSets.frame_height;

	//TODO: include a case ARGB: in switch statement?

    // Determine the color space of the input buffer, depending on the device selected
	switch(dSets.color_format){
		case YUYV:
			bufLength = frameWidth*frameHeight*2;
			framePixelFormat = WINDOW_FORMAT_RGBA_8888;
	        processFrame = &FrameRenderer::processFromYUYV;
			initRenderscript(jenv, rsPath, bufLength);
	        break;
		case UYVY:
			bufLength = frameWidth*frameHeight*2;
	        framePixelFormat = WINDOW_FORMAT_RGBA_8888;
			processFrame = &FrameRenderer::processFromUYVY;
	        initRenderscript(jenv, rsPath, bufLength);
	        break;
		case RGB565:    // no need to init renderscript here as conversion is not necessary
			bufLength = frameWidth*frameHeight*2;
	        framePixelFormat = WINDOW_FORMAT_RGB_565;
			processFrame = &FrameRenderer::processFromRGB;
	        break;
		case RGBA8888:
			bufLength = frameWidth*frameHeight*4;
	        framePixelFormat = WINDOW_FORMAT_RGBA_8888;
	        processFrame = &FrameRenderer::processFromRGB;
	        break;
		default:
			bufLength = frameWidth*frameHeight*2;
	        framePixelFormat = WINDOW_FORMAT_RGBA_8888;
			processFrame = &FrameRenderer::processFromYUYV;
	        initRenderscript(jenv, rsPath, bufLength);
	}
}

FrameRenderer::~FrameRenderer() {

}

void FrameRenderer::initRenderscript(JNIEnv* jenv, jstring rsPath, int bufLength) {

	// Determine the size for the input and output Allocations.  The input Allocation
	// contains Elements of four 8-bit unsigned chars, so we simply divide the input
	// buffer length by 4.  The output Allocation contains RGBA elements which are 32 bits
	// per pixel rather than 16, so we must first multiply our buffer length by two then
	// divide by 4.
	int inAllocationSize = bufLength / 4;
	int outAllocationSize = (bufLength *2) / 4;

	// get the calling activities path to its Cache Directory, necessary to init renderscript
	const char* path = jenv->GetStringUTFChars(rsPath, NULL);
	rs = new RS();
	rs->init(path);
	jenv->ReleaseStringUTFChars(rsPath, path);

	sp<const Element> inElement = Element::U8_4(rs);
	sp<const Element> outElement = Element::RGBA_8888(rs);


	inputAlloc = Allocation::createSized(rs, inElement, inAllocationSize);
	outputAlloc = Allocation::createSized(rs, outElement, outAllocationSize);

	script = new ScriptC_convert(rs);

	script->set_output(outputAlloc);

}

void FrameRenderer::renderFrame(JNIEnv* jenv, jobject surface, CaptureBuffer* inBuffer) {

	ANativeWindow* rWindow = ANativeWindow_fromSurface(jenv, surface);
	ANativeWindow_setBuffersGeometry(rWindow, frameWidth, frameHeight, framePixelFormat);

	// Call the function pointer, which is set in the constructor
	(this->*processFrame)(inBuffer, rWindow);

	ANativeWindow_release(rWindow);
}


void FrameRenderer::processFromYUYV(CaptureBuffer* inBuffer, ANativeWindow* window) {

	inputAlloc->copy1DFrom(inBuffer->start);
	script->forEach_convertFromYUYV(inputAlloc);

	// Write converted colors to the window
	ANativeWindow_Buffer wBuffer;
	if (ANativeWindow_lock(window, &wBuffer, NULL) == 0) {
		outputAlloc->copy1DTo(wBuffer.bits);
		ANativeWindow_unlockAndPost(window);
	}
}



void FrameRenderer::processFromUYVY(CaptureBuffer* inBuffer, ANativeWindow* window) {

	inputAlloc->copy1DFrom(inBuffer->start);
	script->forEach_convertFromUYVY(inputAlloc);

	// Write converted colors to the window
	ANativeWindow_Buffer wBuffer;
	if (ANativeWindow_lock(window, &wBuffer, NULL) == 0) {
		outputAlloc->copy1DTo(wBuffer.bits);
		ANativeWindow_unlockAndPost(window);
	}
}

void FrameRenderer::processFromRGB(CaptureBuffer* inBuffer, ANativeWindow* window) {

	// Write buffer directly to window, no conversion is necessary
	ANativeWindow_Buffer wBuffer;
	if (ANativeWindow_lock(window, &wBuffer, NULL) == 0) {
		memcpy(wBuffer.bits, inBuffer->start,  inBuffer->length);
		ANativeWindow_unlockAndPost(window);
	}
}