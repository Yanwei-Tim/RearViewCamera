/*
 * VideoDevice.cpp
 *
 *  Created on: Nov 4, 2014
 *      Author: Eric
 */

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <malloc.h>
#include <cassert>

VideoDevice::VideoDevice (DeviceSettings devSets) {

	device_sets = devSets;

	buffer_count = 0;
	frame_buffers = nullptr;
	file_descriptor = -1;

    curBufferIndex = 0;

	int area = devSets.frame_width * devSets.frame_height;


}

VideoDevice::~VideoDevice() {
	stop_device();
}

/* Open the video device at the named device node.
 *
 * dev_name - the path to a device, e.g. /dev/video0
 * fd - an output parameter to store the file descriptor once opened.
 *
 * Returns SUCCESS_LOCAL if the device was found and opened and ERROR_LOCAL if
 * an error occurred.
 */
int VideoDevice::open_device() {

	file_descriptor = v4l2_open(device_sets.device_name);

	if (file_descriptor == -1)
		return ERROR_LOCAL;

    return SUCCESS_LOCAL;
}

/* Initialize video device with the given frame size.
 *
 * Initializes the device as a video capture device (must support V4L2) and
 * checks to make sure it has the streaming I/O interface. Configures the device
 * to crop the image to the given dimensions and initailizes a memory mapped
 * frame buffer.
 *
 * Returns SUCCESS_LOCAL if no errors, otherwise ERROR_LOCAL.
 */
int VideoDevice::init_device() {
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    v4l2_std_id std_id;
    unsigned int min;

    CLEAR(cap);
    if(-1 == xioctl(file_descriptor, VIDIOC_QUERYCAP, &cap)) {
        if(EINVAL == errno) {

            return ERROR_LOCAL;
        } else {
            return errnoexit("VIDIOC_QUERYCAP");
        }
    }

    if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOGE("device is not a video capture device");
        return ERROR_LOCAL;
    }

    if(!(cap.capabilities & V4L2_CAP_STREAMING)) {
        LOGE("device does not support streaming i/o");
        return ERROR_LOCAL;
    }

    CLEAR(std_id);
	switch(device_sets.standard_id) {
	case NTSC:
		std_id = V4L2_STD_NTSC;
		break;
	case PAL:
		std_id = V4L2_STD_PAL;
		break;
	default:
		std_id = V4L2_STD_NTSC;
	}

	if(-1 == xioctl(file_descriptor, VIDIOC_S_STD, &std_id)) {
        return errnoexit("VIDIOC_S_STD");
    }

    CLEAR(cropcap);
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if(0 == xioctl(file_descriptor, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect;

        if(-1 == xioctl(file_descriptor, VIDIOC_S_CROP, &crop)) {
            switch(errno) {
                case EINVAL:
                    break;
                default:
                    break;
            }
        }
    }

    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    fmt.fmt.pix.width = device_sets.frame_width;
    fmt.fmt.pix.height = device_sets.frame_height;

    switch(device_sets.color_format){
        case YUYV:
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
            break;
        case UYVY:
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
            break;
        case RGB565:
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB565;
            break;
        default:
            fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    }
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

    if(-1 == xioctl(file_descriptor, VIDIOC_S_FMT, &fmt)) {
        return errnoexit("VIDIOC_S_FMT");
    }


    return init_mmap();
}

/* Initialize memory mapped buffers for video frames.
 *
 * fd - a valid file descriptor pointing to the camera device.
 *
 * Returns SUCCESS_LOCAL if no errors, otherwise ERROR_LOCAL.
 */
int VideoDevice::init_mmap() {
	struct v4l2_requestbuffers req;
	CLEAR(req);
	req.count = device_sets.num_buffers;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if(-1 == xioctl(file_descriptor, VIDIOC_REQBUFS, &req)) {
		if(EINVAL == errno) {
			LOGE("device does not support memory mapping");
			return ERROR_LOCAL;
		} else {
			return errnoexit("VIDIOC_REQBUFS");
		}
	}

	if(req.count < 2) {
		LOGE("Insufficient buffer memory");
		return ERROR_LOCAL;
	}

	frame_buffers = (CaptureBuffer *)calloc(req.count, sizeof(*frame_buffers));
	if(!frame_buffers) {
		LOGE("Out of memory");
		return ERROR_LOCAL;
	}

	for(buffer_count = 0; buffer_count < req.count; ++buffer_count) {
		struct v4l2_buffer buf;
		CLEAR(buf);

		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = buffer_count;

		if(-1 == xioctl(file_descriptor, VIDIOC_QUERYBUF, &buf)) {
			return errnoexit("VIDIOC_QUERYBUF");
		}

		frame_buffers[buffer_count].length = buf.length;
		frame_buffers[buffer_count].start = mmap(NULL, buf.length,
		                                         PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, buf.m.offset);

		if(MAP_FAILED == frame_buffers[buffer_count].start) {
			return errnoexit("mmap");
		}
	}

	LOGI("Frame Buffer Length (bytes): %i", frame_buffers[0].length);

	return SUCCESS_LOCAL;
}

/* Begins capturing video frames from a previously initialized device.
 *
 * The buffers in FRAME_BUFFERS are handed off to the device.
 *
 * fd - a valid file descriptor to the device.
 *
 * Returns SUCCESS_LOCAL if no errors, otherwise ERROR_LOCAL.
 */
int VideoDevice::start_capture() {
    unsigned int i;
    enum v4l2_buf_type type;

    for(i = 0; i < buffer_count; ++i) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if(-1 == xioctl(file_descriptor, VIDIOC_QBUF, &buf)) {
            return errnoexit("VIDIOC_QBUF");
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(-1 == xioctl(file_descriptor, VIDIOC_STREAMON, &type)) {
        return errnoexit("VIDIOC_STREAMON");
    }

    return SUCCESS_LOCAL;
}

/* Request a frame of video from the device to be output into the rgb
 * and y buffers.
 *
 * If the descriptor is not valid, no frame will be read.
 *
 * Returns a pointer to the latest buffer read into memory from the device
 *
 */
CaptureBuffer * VideoDevice::process_capture() {

	if(file_descriptor == -1) {
		return NULL;
	}

	for(;;) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(file_descriptor, &fds);

		struct timeval tv;
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		int result = select(file_descriptor + 1, &fds, NULL, NULL, &tv);
		if(-1 == result) {
			if(EINTR == errno) {
				continue;
			}
			errnoexit("select");
		} else if(0 == result) {
			LOGE("select timeout, likely can't process chosen TV standard");
			sleep(1);
			break;
		}

		if (read_frame() == 1) {
            return &frame_buffers[curBufferIndex];
		}
	}

	return NULL;
}

/* Read a single frame of video from the device into a buffer.
 *
 *
 * Returns SUCCESS_LOCAL if no errors, otherwise ERROR_LOCAL.
 */
int VideoDevice::read_frame() {
    struct v4l2_buffer buf;
    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if(-1 == xioctl(file_descriptor, VIDIOC_DQBUF, &buf)) {
        switch(errno) {
            case EAGAIN:
                return 0;
            case EIO:
            default:
				return 1;
                //return errnoexit("VIDIOC_DQBUF");
        }
    }

    assert(buf.index < buffer_count);


    // convert and copy the buffer for rendering
    curBufferIndex = (int)(buf.index);

    if(-1 == xioctl(file_descriptor, VIDIOC_QBUF, &buf)) {
    	return errnoexit("VIDIOC_QBUF");
    }

    return 1;
}

/* Stop capturing, uninitialize the device and free all memory. */
void VideoDevice::stop_device() {
	stop_capture();
	uninit_device();
	close_device();

}

/* Unconfigure the video device for capturing.
 *
 * Returns SUCCESS_LOCAL if no errors, otherwise ERROR_LOCAL.
 */
int VideoDevice::stop_capture() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(-1 != file_descriptor && -1 == xioctl(file_descriptor, VIDIOC_STREAMOFF, &type)) {
        return errnoexit("VIDIOC_STREAMOFF");
    }

    return SUCCESS_LOCAL;
}

/* Unmap and free memory-mapped frame buffers from the device.
 *
 * Returns SUCCESS_LOCAL if no errors, otherwise ERROR_LOCAL.
 */
int VideoDevice::uninit_device() {
	for (unsigned int i = 0; i < buffer_count; ++i) {
		if (-1 == munmap(frame_buffers[i].start, frame_buffers[i].length)) {
			return errnoexit("munmap");
		}
	}

	free(frame_buffers);
	return SUCCESS_LOCAL;
}


/* Close a file descriptor.
 *
 * fd - a pointer to the descriptor to close, which will be set to -1 on success
 *      or fail.
 *
 * Returns SUCCESS_LOCAL if no errors, otherwise ERROR_LOCAL.
 */
int VideoDevice::close_device() {

	return v4l2_close(file_descriptor);
}


/**
 * detect_device - Helper function to attempt to find a specific device
 */
DeviceType VideoDevice::detect_device(const char* dev_name) {
	struct v4l2_capability cap;
	int fd = -1;
	DeviceType dev_type = NO_DEVICE;


	fd = v4l2_open(dev_name);
	if (fd == -1)
	{
		return NO_DEVICE;
	}

	// Get device capabilities
	CLEAR(cap);
	if(-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
		return NO_DEVICE;
	}

	LOGI("Driver detected as: %s", cap.driver);


	if(strncmp((const char*)cap.driver, "usbtv", 5) == 0)
		dev_type = UTV007;
	else if(strncmp((const char*)cap.driver,"em28xx", 6) == 0)
		dev_type = EMPIA;
	else if(strncmp((const char*)cap.driver, "stk1160", 7) == 0)
		dev_type = STK1160;
	else if(strncmp((const char*)cap.driver, "smi2021", 7) == 0)
		dev_type = SOMAGIC;
	else
		dev_type = NO_DEVICE;

	// Close Device
	if(v4l2_close(fd) != SUCCESS_LOCAL) {
		return NO_DEVICE;
	}

	return dev_type;
}


// Helper functions to open and close devices, to keep from repeating code
int VideoDevice::v4l2_open(const char* dev_name) {

	struct stat st;
	int fd = - 1;
	if(-1 == stat(dev_name, &st)) {
		LOGE("Cannot identify '%s': %d, %s", dev_name, errno, strerror(errno));
		return ERROR_LOCAL;
	}

	if(!S_ISCHR(st.st_mode)) {
		LOGE("%s is not a valid device", dev_name);
		return ERROR_LOCAL;
	}

	fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
	if(-1 == fd) {
		LOGE("Cannot open '%s': %d, %s", dev_name, errno, strerror(errno));
		if(EACCES == errno) {
			LOGE("Insufficient permissions on '%s': %d, %s", dev_name, errno,
					strerror(errno));
		}
		return ERROR_LOCAL;
	}

	return fd;
}

int VideoDevice::v4l2_close(int fd) {
	int result = SUCCESS_LOCAL;
	if(-1 != fd && -1 == close(fd)) {
		result = errnoexit("close");
	}
	fd = -1;
	return result;
}

