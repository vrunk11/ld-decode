/************************************************************************

    outputwriter.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018-2021 Chad Page
    Copyright (C) 2020-2021 Adam Sampson
    Copyright (C) 2021 Phillip Blucas

    This file is part of ld-decode-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

#include "outputwriter.h"

#include "componentframe.h"

// Limits, zero points and scaling factors (from 0-1) for Y'CbCr colour representations
// [Poynton ch25 p305] [BT.601-7 sec 2.5.3]
static constexpr double Y_MIN   = 1.0    * 256.0;
static constexpr double Y_ZERO  = 16.0   * 256.0;
static constexpr double Y_SCALE = 219.0  * 256.0;
static constexpr double Y_MAX   = 254.75 * 256.0;
static constexpr double C_MIN   = 1.0    * 256.0;
static constexpr double C_ZERO  = 128.0  * 256.0;
static constexpr double C_SCALE = 112.0  * 256.0;
static constexpr double C_MAX   = 254.75 * 256.0;

// ITU-R BT.601-7
// [Poynton eq 25.1 p303 and eq 25.5 p307]
static constexpr double ONE_MINUS_Kb = 1.0 - 0.114;
static constexpr double ONE_MINUS_Kr = 1.0 - 0.299;

// kB = sqrt(209556997.0 / 96146491.0) / 3.0
// kR = sqrt(221990474.0 / 288439473.0)
// [Poynton eq 28.1 p336]
static constexpr double kB = 0.49211104112248356308804691718185;
static constexpr double kR = 0.87728321993817866838972487283129;

void OutputWriter::updateConfiguration(LdDecodeMetaData::VideoParameters &_videoParameters,
                                       const OutputWriter::Configuration &_config)
{
    config = _config;
    videoParameters = _videoParameters;
    topPadLines = 0;
    bottomPadLines = 0;

    activeWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
    activeHeight = videoParameters.lastActiveFrameLine - videoParameters.firstActiveFrameLine;
    outputHeight = activeHeight;

    if (config.paddingAmount > 1) {
        // Some video codecs require the width and height of a video to be divisible by
        // a given number of samples on each axis.
        
        // Expand horizontal active region so the width is divisible by the specified padding factor.
        while (true) {
			activeWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;
            if ((activeWidth % config.paddingAmount) == 0) {
                break;
            }

            // Add pixels to the right and left sides in turn, to keep the active area centred
            if ((activeWidth % 2) == 0) {
                videoParameters.activeVideoEnd++;
            } else {
                videoParameters.activeVideoStart--;
            }
        }

        // Insert empty padding lines so the height is divisible by by the specified padding factor.
        while (true) {
            outputHeight = topPadLines + activeHeight + bottomPadLines;
            if ((outputHeight % config.paddingAmount) == 0) {
                break;
            }

            // Add lines to the bottom and top in turn, to keep the active area centred
            if ((outputHeight % 2) == 0) {
                bottomPadLines++;
            } else {
                topPadLines++;
            }
        }

        // Update the caller's copy, now we've adjusted the active area
        _videoParameters = videoParameters;
    }
	outputWidth = config.useResampling ? config.resampleWidth : activeWidth;
}

const char *OutputWriter::getPixelName() const
{
	if(config.is8bit)
	{
		switch (config.pixelFormat) {
		case RGB:
			return "RGB24";
		case YUV444:
			return "YUV444p";
		case YUV422:
			return "YUV422p";
		case YUV411:
			return "YUV411p";
		case GRAY:
			return "GRAY8";
		default:
			return "unknown";
		}
	}
	else
	{
		switch (config.pixelFormat) {
		case RGB:
			return "RGB48";
		case YUV444:
			return "YUV444p16";
		case YUV422:
			return "YUV422p16";
		case GRAY:
			return "GRAY16";
		default:
			return "unknown";
		}
	}
}

void OutputWriter::printOutputInfo() const
{
    // Show output information to the user
    const qint32 frameHeight = (videoParameters.fieldHeight * 2) - 1;
    qInfo() << "Input video of" << videoParameters.fieldWidth << "x" << frameHeight
            << "will be colourised and trimmed to" << outputWidth << "x" << outputHeight
            << getPixelName() << "frames";
}

QByteArray OutputWriter::getY4mHeader() const
{
    // return if output dont needs a header
    if (!config.useOutputHeader || config.outputHeader == "raw") {
        return QByteArray();
    }

    QString header;
    QTextStream str(&header);
	
	if(config.outputHeader == "y4m")
	{
		str << "YUV4MPEG2";

		// Frame size
		config.useResampling ? str << " W" << config.resampleWidth : str << " W" << activeWidth;
		str << " H" << outputHeight;

		// Frame rate
		if (videoParameters.system == PAL) {
			str << " F25:1";
		} else {
			str << " F30000:1001";
		}

		// Field order
		if (videoParameters.firstActiveFrameLine % 2 ^ topPadLines % 2) {
			str << " Ib";
		} else {
			str << " It";
		}

		// Pixel aspect ratio
		if(config.useResampling)
		{
			const int height = (videoParameters.system == PAL ? 576 : 488);
			if (videoParameters.isWidescreen) {
				// widescreen DAR = 16:9
				auto num = 16 * height;
				auto den =  9 * config.resampleWidth;
				auto g   = std::gcd(num, den);
				str << " A" << (num/g) << ":" << (den/g);
			} else {
				// standard DAR = 4:3
				auto num =  4 * height;
				auto den =  3 * config.resampleWidth;
				auto g   = std::gcd(num, den);
				str << " A" << (num/g) << ":" << (den/g);
			}
		}
		else
		{
			// Follows EBU R92 and SMPTE RP 187 except that values are scaled from
			// BT.601 sampling (13.5 MHz) to 4fSC
			if (videoParameters.system == PAL) {
				if (videoParameters.isWidescreen) {
					str << " A865:779"; // (16 / 9) * (576 / (702 * 4*fSC / 13.5))
				} else {
					str << " A259:311"; // (4 / 3) * (576 / (702 * 4*fSC / 13.5))
				}
			} else {
				if (videoParameters.isWidescreen) {
					str << " A25:22"; // (16 / 9) * (480 / (708 * 4*fSC / 13.5))
				} else {
					str << " A352:413"; // (4 / 3) * (480 / (708 * 4*fSC / 13.5))
				}
			}
		}

		// Pixel format
		if(config.is8bit)
		{
			switch (config.pixelFormat) {
				case YUV444:
					str << " C444p XCOLORRANGE=LIMITED";
					break;
				case YUV422:
					str << " C422p XCOLORRANGE=LIMITED";
					break;
				case YUV411:
					str << " C411p XCOLORRANGE=LIMITED";
					break;
				case GRAY:
					str << " Cmono XCOLORRANGE=LIMITED";
					break;
				default:
					qFatal("pixel format not supported in yuv4mpeg header");
					break;
			}
		}
		else
		{
			switch (config.pixelFormat) {
				case YUV444:
					str << " C444p16 XCOLORRANGE=LIMITED";
					break;
				case YUV422:
					str << " C422p16 XCOLORRANGE=LIMITED";
					break;
				case YUV411:
					str << " C411p XCOLORRANGE=LIMITED";
					break;
				case GRAY:
					str << " Cmono16 XCOLORRANGE=LIMITED";
					break;
				default:
					qFatal("pixel format not supported in yuv4mpeg header");
					break;
			}
		}

		str << "\n";
	}
	else
	{
		return QByteArray();
	}
    return header.toUtf8();
}

void OutputWriter::initVideoEncoding(AVFormatContext* &fmt_ctx, AVStream* &stream,const AVCodec* &codec, AVCodecContext* &codec_ctx, AVDictionary* &codec_opt, AVFrame* &frame, QString outputFileName, AVDictionary* metadata)
{
	int width = outputWidth;
	int height = outputHeight;
	//be sure to support only specified format
	if(config.outputHeader == "mkv")
	{
		avformat_alloc_output_context2(&fmt_ctx, nullptr, "matroska", nullptr); // format_name: "nut" or "matroska"
	}
	else
	{
		avformat_alloc_output_context2(&fmt_ctx, nullptr, "nut", nullptr); // format_name: "nut" or "matroska"
	}
	
	if (!fmt_ctx) {
		qFatal("AV format context error");
	}
	
	stream = avformat_new_stream(fmt_ctx, nullptr);
	if (!stream){
		qFatal("Could not create new stream");
	}
	
	if(config.useFFV1)
	{
		codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
		if (!codec) {
			qFatal("FFV1 encoder not found in libavcodec");
		}
	}
	else
	{
		codec = avcodec_find_encoder(AV_CODEC_ID_RAWVIDEO);
		if (!codec) {
			qFatal("RAWVIDEO encoder not found in libavcodec");
		}
	}
	
	codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		qFatal("Could not create a codec context");
	}
	
	frame = av_frame_alloc();
	if (!frame) {
		qFatal("Could not create a frame");
	}
	
	fmt_ctx->metadata = metadata;
	
	codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
	codec_ctx->width     = width;
	codec_ctx->height    = height;
	
	// Field order
	frame->interlaced_frame = 1; // 1 for interlaced, 0 for progressive
	if (videoParameters.firstActiveFrameLine % 2 ^ topPadLines % 2) {
		codec_ctx->field_order = AV_FIELD_BT;
		stream->codecpar->field_order = AV_FIELD_BT;
		frame->top_field_first = 0;  // 1 if top field is first, 0 otherwise
	} else {
		codec_ctx->field_order = AV_FIELD_TT;
		codec_ctx->field_order = AV_FIELD_TT;
		stream->codecpar->field_order = AV_FIELD_TT;
		frame->top_field_first = 1;  // 1 if top field is first, 0 otherwise
	}
	
	if (videoParameters.isWidescreen) {
		// widescreen DAR = 16:9
		auto num = 16 * height;
		auto den =  9 * width;
		auto g   = std::gcd(num, den);
		codec_ctx->sample_aspect_ratio = (AVRational){(num/g), (den/g)};
		stream->codecpar->sample_aspect_ratio = (AVRational){(num/g), (den/g)};
	} else {
		// standard DAR = 4:3
		auto num =  4 * height;
		auto den =  3 * width;
		auto g   = std::gcd(num, den);
		codec_ctx->sample_aspect_ratio = (AVRational){(num/g), (den/g)};//str << " A" << (num/g) << ":" << (den/g);
		stream->codecpar->sample_aspect_ratio = (AVRational){(num/g), (den/g)};
	}
	
	if (videoParameters.system == PAL) {
		codec_ctx->time_base = (AVRational) {1,25};
	}
	else{
		codec_ctx->time_base = (AVRational) {1001,30000};
	}

	// Pixel format
	uint32_t fourcc_code = 0;
    if(config.is8bit)
    {
        codec_ctx->bits_per_raw_sample = 8;
        switch (config.pixelFormat) {
        case YUV444:
            codec_ctx->pix_fmt = AV_PIX_FMT_YUV444P;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV444P);  // Standard FourCC for YUV444P
            break;
        case YUV422:
            codec_ctx->pix_fmt = AV_PIX_FMT_YUV422P;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV422P);  // Standard FourCC for YUV422P
            break;
        case YUV411:
            codec_ctx->pix_fmt = AV_PIX_FMT_YUV411P;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV411P);  // Standard FourCC for YUV411P
            break;
        case GRAY:
            codec_ctx->pix_fmt = AV_PIX_FMT_GRAY8;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_GRAY8);  // Standard FourCC for GRAY8
            break;
        default:
            codec_ctx->pix_fmt = AV_PIX_FMT_BGR0;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_BGR0);  // Standard FourCC for BGR0
            break;
        }
    }
    else
    {
        codec_ctx->bits_per_raw_sample = 16;
        switch (config.pixelFormat) {
        case YUV444:
            codec_ctx->pix_fmt = AV_PIX_FMT_YUV444P16LE;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV444P16LE);  // Custom FourCC for YUV444P16LE
            break;
        case YUV422:
            codec_ctx->pix_fmt = AV_PIX_FMT_YUV422P16LE;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV422P16LE);  // Custom FourCC for YUV422P16LE
            break;
        case GRAY:
            codec_ctx->pix_fmt = AV_PIX_FMT_GRAY16LE;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_GRAY16LE);  // Custom FourCC for GRAY16LE
            break;
        default:
            codec_ctx->pix_fmt = AV_PIX_FMT_RGB48LE;
            fourcc_code = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_RGB48LE);  // Custom FourCC for RGB48LE
            break;
        }
    }
	//set code to 0 to not break ffv1
	if(config.useFFV1)
	{
		fourcc_code = 0;
	}
	
	codec_ctx->codec_tag = fourcc_code;
	// global header required by Matroska/NUT
    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
	{
		codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		av_dict_set(&codec_opt, "global_header", "1", 0);
	}			
	
	// encoder option
	av_dict_set(&codec_opt, "level",    "3", 0);
	av_dict_set(&codec_opt, "coder",    "1", 0);
	av_dict_set(&codec_opt, "context",  "1", 0);
	//av_dict_set(&codec_opt, "g",        "16", 0);
	av_dict_set(&codec_opt, "slices",  "4", 0);
	av_dict_set(&codec_opt, "slicecrc", "1", 0);
	//av_dict_set(&codec_opt, "threads", "10", 0);
	
	// open the encoder (copies extradata into ctx->extradata)
	if (avcodec_open2(codec_ctx, codec, &codec_opt) < 0) {
		qFatal("Could not open codec");
	}
	
	// copy context (incl. extradata) into stream parameters
	if (avcodec_parameters_from_context(stream->codecpar, codec_ctx) < 0) {
		qFatal("Failed to copy codec parameters");
	}
	
	// initialise stream
	stream->sample_aspect_ratio = codec_ctx->sample_aspect_ratio;
	stream->codecpar->width  = width;
	stream->codecpar->height = height;

	// ensure the muxer writes your chosen codec and pix_fmt tag, too:
	stream->codecpar->codec_id  = codec_ctx->codec_id;
	stream->codecpar->codec_tag = fourcc_code;//fourcc_code;
	stream->codecpar->format    = codec_ctx->pix_fmt;
	
	// initialise frame
	frame->format = codec_ctx->pix_fmt;
	frame->width = width;
	frame->height = height;
	
	// Allocate frame buffers
    if (av_frame_get_buffer(frame, 0) < 0) {
        qFatal("Could not allocate frame buffers");
    }
	
	if (avio_open(&fmt_ctx->pb, outputFileName.toStdString().c_str(), AVIO_FLAG_WRITE) < 0) {
		qFatal("error opening output file");
	}
	
	if (avformat_write_header(fmt_ctx, nullptr) < 0)
	{
		qFatal("Error writing header");
	}
	return;
}

QByteArray OutputWriter::getFrameHeader() const
{
    // Only yuv4mpeg output needs a header
    if (!config.useOutputHeader) {
        return QByteArray();
    }

    return QStringLiteral("FRAME\n").toUtf8();
}

void OutputWriter::convert(ComponentFrame &componentFrameIn, OutputFrame &outputFrame) const
{
	qint32 outSize = (outputWidth * outputHeight);
	ComponentFrame componentFrameResample;
	
	if(config.useResampling)
	{
		//init only if we resample
		componentFrameResample.init(videoParameters,false);
		
		//soxr related data
		size_t flushDone = 0;
		size_t idone = 0, odone = 0;
		soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT64_I, SOXR_FLOAT64_I);
		soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_HQ, SOXR_ROLLOFF_SMALL);
		soxr_runtime_spec_t const runtime_spec = soxr_runtime_spec(1);
		
		const double resizeRatio = static_cast<double> (outputWidth) / activeWidth;

		//compute inactive area
		const double leftPad  = videoParameters.activeVideoStart * resizeRatio;
		const double rightPad = (videoParameters.fieldWidth - videoParameters.activeVideoEnd) * resizeRatio;
		double inactiveOutSize = qFloor(leftPad + rightPad);
		
		//pad the fullframe to a multiple off output width to avoid fractional size (we multiply by input field width to be sure it works for all size)
		const double inResampleSize = (componentFrameIn.getWidth()*componentFrameIn.getWidth());
		const double outResampleSize = ((outputWidth + inactiveOutSize)*componentFrameIn.getWidth());

		//get real input size without padding
		const double inSize = componentFrameIn.getWidth() * componentFrameIn.getHeight();
		const double outSize = (outputWidth + inactiveOutSize) * componentFrameIn.getHeight();
		
		componentFrameResample.setWidth(outputWidth + inactiveOutSize);
		
		//resize to be safe
		componentFrameIn.getY()->resize(inSize);
		componentFrameIn.getU()->resize(inSize);
		componentFrameIn.getV()->resize(inSize);
		
		//use 4:4:4 size to be safe
		componentFrameResample.getY()->resize(outSize);
		componentFrameResample.getU()->resize(outSize);
		componentFrameResample.getV()->resize(outSize);
		
		//resampling Y if size is not native
		if(config.resampleWidth != (videoParameters.activeVideoEnd - videoParameters.activeVideoStart))
		{
			soxr_error_t errY;
			soxr_t soxrY = soxr_create(inResampleSize, outResampleSize, 1, &errY, &io_spec, &q_spec, &runtime_spec);
			errY = soxr_process(soxrY, componentFrameIn.getY()->data(), inSize, &idone, componentFrameResample.getY()->data(), outSize, &odone);
			soxr_delete(soxrY);
		}
		else
		{
			//map the luma to the resampled component frame
			componentFrameResample.setY(*componentFrameIn.getY());
		}
		
		//resample U and V if its different than native or not 4:4:4
		if(config.resampleWidth != (videoParameters.activeVideoEnd - videoParameters.activeVideoStart) || config.pixelFormat == YUV422 || config.pixelFormat == YUV411)
		{
			if(config.pixelFormat != GRAY)
			{
				soxr_error_t errU;
				soxr_error_t errV;
				
				if(config.pixelFormat == YUV444 || config.pixelFormat == RGB)
				{
					soxr_t soxrU = soxr_create(inResampleSize, outResampleSize, 1, &errU, &io_spec, &q_spec, &runtime_spec);
					soxr_t soxrV = soxr_create(inResampleSize, outResampleSize, 1, &errV, &io_spec, &q_spec, &runtime_spec);
					
					errU = soxr_process(soxrU, componentFrameIn.getU()->data(), inSize, &idone, componentFrameResample.getU()->data(), outSize, &odone);
					errV = soxr_process(soxrV, componentFrameIn.getV()->data(), inSize, &idone, componentFrameResample.getV()->data(), outSize, &odone);
					
					soxr_delete(soxrU);
					soxr_delete(soxrV);
				}
				else if(config.pixelFormat == YUV422)
				{
					
					soxr_t soxrU = soxr_create(inResampleSize, outResampleSize/2.0, 1, &errU, &io_spec, &q_spec, &runtime_spec);
					soxr_t soxrV = soxr_create(inResampleSize, outResampleSize/2.0, 1, &errV, &io_spec, &q_spec, &runtime_spec);
					
					errU = soxr_process(soxrU, componentFrameIn.getU()->data(), inSize, &idone, componentFrameResample.getU()->data(), qFloor(outSize/2.0), &odone);
					errV = soxr_process(soxrV, componentFrameIn.getV()->data(), inSize, &idone, componentFrameResample.getV()->data(), qFloor(outSize/2.0), &odone);
					
					soxr_delete(soxrU);
					soxr_delete(soxrV);
				}
				else//yuv411p
				{
					soxr_t soxrU = soxr_create(inResampleSize, outResampleSize/4.0, 1, &errU, &io_spec, &q_spec, &runtime_spec);
					soxr_t soxrV = soxr_create(inResampleSize, outResampleSize/4.0, 1, &errV, &io_spec, &q_spec, &runtime_spec);
					
					errU = soxr_process(soxrU, componentFrameIn.getU()->data(), inSize, &idone, componentFrameResample.getU()->data(), qFloor(outSize/4.0), &odone);
					errV = soxr_process(soxrV, componentFrameIn.getV()->data(), inSize, &idone, componentFrameResample.getV()->data(), qFloor(outSize/4.0), &odone);
					
					soxr_delete(soxrU);
					soxr_delete(soxrV);
				}
			}
		}
		else
		{
			//map the chroma to the resampled component frame
			componentFrameResample.setU(*componentFrameIn.getU());
			componentFrameResample.setV(*componentFrameIn.getV());
		}
	}
	
    // Work out the number of output values, and resize the vector accordingly
    switch (config.pixelFormat) {
    case RGB:
    case YUV444:
        outSize *= 3;
        break;
	case YUV422:
        outSize *= 2;
        break;
	case YUV411:
        outSize = qRound(outSize + (outSize/2.0));// y + (U/4) + (V/4) = y + (C/2) = y + (y/2)
        break;
    case GRAY:
        break;
    }
    outputFrame.resize(outSize);

    // Clear padding
    clearPadLines(0, topPadLines, outputFrame);
    clearPadLines(outputHeight - bottomPadLines, bottomPadLines, outputFrame);
	
	if(config.useResampling)
	{
		// Convert active lines
		for (qint32 y = 0; y < activeHeight; y++) {
			convertLine(y, componentFrameResample, outputFrame);
		}
	}
	else
	{
		// Convert active lines
		for (qint32 y = 0; y < activeHeight; y++) {
			convertLine(y, componentFrameIn, outputFrame);
		}
	}
}

void OutputWriter::clearPadLines(qint32 firstLine, qint32 numLines, OutputFrame &outputFrame) const
{
	PixelFormat pixelFormat = config.pixelFormat;
    switch (pixelFormat) {
        case RGB: {
            // Fill with RGB black
            quint16 *out = outputFrame.data() + (outputWidth * firstLine * 3);

            for (qint32 i = 0; i < numLines * outputWidth * 3; i++) {
                out[i] = 0;
            }

            break;
        }
        case YUV444: {
            // Fill Y with black, no chroma
            quint16 *outY  = outputFrame.data() + (outputWidth * firstLine);
            quint16 *outCB = outY + (outputWidth * outputHeight);
            quint16 *outCR = outCB + (outputWidth * outputHeight);

            for (qint32 i = 0; i < numLines * outputWidth; i++) {
                outY[i]  = static_cast<quint16>(Y_ZERO);
                outCB[i] = static_cast<quint16>(C_ZERO);
                outCR[i] = static_cast<quint16>(C_ZERO);
            }

            break;
        }
		case YUV422: {
			int yPlaneSize  = outputWidth * outputHeight;
			int cWidth      = outputWidth / 2;
			int cPlaneSize  = cWidth * outputHeight;

			quint16* baseY  = outputFrame.data();
			quint16* baseCb = baseY + yPlaneSize;
			quint16* baseCr = baseCb + cPlaneSize;

			// Y padding
			quint16* outY = baseY + firstLine * outputWidth;
			for (int i = 0; i < numLines * outputWidth; ++i)
				outY[i] = Y_ZERO;

			// Cb/Cr padding
			quint16* outCb = baseCb + firstLine * cWidth;
			quint16* outCr = baseCr + firstLine * cWidth;
			if (firstLine*2 < activeHeight + videoParameters.firstActiveFrameLine*2) {
				for (int i = 0; i < numLines * cWidth; ++i) {
					outCb[i] = C_ZERO;
					outCr[i] = C_ZERO;
				}
			}
			break;
		}
		case YUV411: {
			// total samples per plane
			const int yPlaneSize  = outputWidth * outputHeight;
			const int cLineWidth  = outputWidth / 4;    // chroma samples per line
			const int cPlaneSize  = cLineWidth * outputHeight;

			// plane bases
			quint16* baseY  = outputFrame.data();
			quint16* baseCb = baseY + yPlaneSize;
			quint16* baseCr = baseCb + cPlaneSize;

			// pad Y: numLines rows of width samples starting at firstLine
			quint16* outY = baseY + firstLine * outputWidth;
			for (int i = 0; i < numLines * outputWidth; ++i) {
				outY[i] = static_cast<quint16>(Y_ZERO);
			}

			// pad Cb/Cr: numLines rows of width/4 samples
			quint16* outCb = baseCb + firstLine * cLineWidth;
			quint16* outCr = baseCr + firstLine * cLineWidth;
			// only pad if within the active (non-flushed) region:
			if (firstLine * 4 < activeHeight + videoParameters.firstActiveFrameLine * 4) {
				for (int i = 0; i < numLines * cLineWidth; ++i) {
					outCb[i] = static_cast<quint16>(C_ZERO);
					outCr[i] = static_cast<quint16>(C_ZERO);
				}
			}
			break;
		}
        case GRAY: {
            // Fill with black
            quint16 *out = outputFrame.data() + (outputWidth * firstLine);

            for (qint32 i = 0; i < numLines * outputWidth; i++) {
                out[i] = static_cast<quint16>(Y_ZERO);
            }

            break;
        }
    }
}

void OutputWriter::convertLine(qint32 lineNumber, const ComponentFrame &componentFrame, OutputFrame &outputFrame) const
{
    // Get pointers to the component data for the active region
    const qint32 inputLine = videoParameters.firstActiveFrameLine + lineNumber;
	
	const double resizeRatio = static_cast<double> (outputWidth) / activeWidth;
	
    const double *inY = componentFrame.y(inputLine) + static_cast<quint32> (qRound(videoParameters.activeVideoStart * resizeRatio));
    // Not used if output is GRAY16
    const double *inU = (config.pixelFormat != GRAY)
                         ? componentFrame.u(inputLine) + static_cast<quint32> (qRound(videoParameters.activeVideoStart * resizeRatio))
                         : nullptr;
    const double *inV = (config.pixelFormat != GRAY)
                         ? componentFrame.v(inputLine) + static_cast<quint32> (qRound(videoParameters.activeVideoStart * resizeRatio))
                         : nullptr;

    const qint32 outputLine = topPadLines + lineNumber;

    const double yOffset = videoParameters.black16bIre;
    double yRange = videoParameters.white16bIre - videoParameters.black16bIre;
    const double uvRange = yRange;
	
    switch (config.pixelFormat) {
        case RGB: {
            // Convert Y'UV to full-range R'G'B' [Poynton eq 28.6 p337]
            quint16 *out = outputFrame.data() + (outputWidth * outputLine * 3);

            const double yScale = 65535.0 / yRange;
            const double uvScale = 65535.0 / uvRange;

            for (qint32 x = 0; x < outputWidth; x++) {
                // Scale Y'UV to 0-65535
                const double rY = qBound(0.0, (inY[x] - yOffset) * yScale, 65535.0);
                const double rU = inU[x] * uvScale;
                const double rV = inV[x] * uvScale;

                // Convert Y'UV to R'G'B'
                const qint32 pos = x * 3;
                out[pos]     = static_cast<quint16>(qBound(0.0, rY                    + (1.139883 * rV),  65535.0));
                out[pos + 1] = static_cast<quint16>(qBound(0.0, rY + (-0.394642 * rU) + (-0.580622 * rV), 65535.0));
                out[pos + 2] = static_cast<quint16>(qBound(0.0, rY + (2.032062 * rU),                     65535.0));
            }

            break;
        }
        case YUV444: {
            // Convert Y'UV to Y'CbCr [Poynton eq 25.5 p307]
            quint16 *outY  = outputFrame.data() + (outputWidth * outputLine);
            quint16 *outCB = outY + (outputWidth * outputHeight);
            quint16 *outCR = outCB + (outputWidth * outputHeight);

            const double yScale = Y_SCALE / yRange;
            const double cbScale = (C_SCALE / (ONE_MINUS_Kb * kB)) / uvRange;
            const double crScale = (C_SCALE / (ONE_MINUS_Kr * kR)) / uvRange;

            for (qint32 x = 0; x < outputWidth; x++) {
                outY[x]  = static_cast<quint16>(qBound(Y_MIN, ((inY[x] - yOffset) * yScale)  + Y_ZERO, Y_MAX));
                outCB[x] = static_cast<quint16>(qBound(C_MIN, (inU[x]             * cbScale) + C_ZERO, C_MAX));
                outCR[x] = static_cast<quint16>(qBound(C_MIN, (inV[x]             * crScale) + C_ZERO, C_MAX));
            }

            break;
        }
		case YUV422: {
			const double resizeRatioChroma = resizeRatio;
			const double leftPad  = qRound(videoParameters.activeVideoStart * resizeRatio);
			const double rightPad = qRound((videoParameters.fieldWidth - videoParameters.activeVideoEnd) * resizeRatio);
			const double inactiveOutSize = leftPad + rightPad;
			const double inactiveOutSizeChroma = inactiveOutSize/2.0;
			
			quint16 *outY  = outputFrame.data() + (outputWidth * outputLine);
			const double yScale = Y_SCALE / yRange;
			
			for (qint32 x = 0; x < outputWidth; x++) {
				outY[x] = static_cast<quint16>(qBound(Y_MIN, ((inY[x] - yOffset) * yScale) + Y_ZERO, Y_MAX));
			}

			if(inputLine*2 < activeHeight + (videoParameters.firstActiveFrameLine * 2))
			{
				const double *inputU = componentFrame.u(inputLine - qRound((videoParameters.firstActiveFrameLine/2.0))) + qRound((videoParameters.activeVideoStart * resizeRatioChroma)/2.0);
				const double *inputV = componentFrame.v(inputLine - qRound((videoParameters.firstActiveFrameLine/2.0))) + qRound((videoParameters.activeVideoStart * resizeRatioChroma)/2.0);
				
				// Convert Y'UV to Y'CbCr [Poynton eq 25.5 p307]
				quint16 *outCB = outY + (outputWidth * (outputHeight));
				if (videoParameters.system != PAL) {
					outCB = outY + (outputWidth * (outputHeight-1));
				}
				quint16 *outCR = outCB + qFloor(outputWidth * (outputHeight/2.0));

				const double cbScale = (C_SCALE / (ONE_MINUS_Kb * kB)) / uvRange;
				const double crScale = (C_SCALE / (ONE_MINUS_Kr * kR)) / uvRange;

				for (qint32 x = 0; x < outputWidth; x++) {
					if(x < outputWidth/2.0 )
					{
						outCB[x] = static_cast<quint16>(qBound(C_MIN, (inputU[x] * cbScale) + C_ZERO, C_MAX));
						outCR[x] = static_cast<quint16>(qBound(C_MIN, (inputV[x] * crScale) + C_ZERO, C_MAX));
					}
					else
					{
						outCB[x] = static_cast<quint16>(qBound(C_MIN, (inputU[x+qRound(inactiveOutSizeChroma)] * cbScale) + C_ZERO, C_MAX));
						outCR[x] = static_cast<quint16>(qBound(C_MIN, (inputV[x+qRound(inactiveOutSizeChroma)] * crScale) + C_ZERO, C_MAX));
					}
				}
			}
            break;
        }
		case YUV411: {
			const double resizeRatioChroma = resizeRatio;
			const double leftPad  = videoParameters.activeVideoStart * resizeRatio;
			const double rightPad = (videoParameters.fieldWidth - videoParameters.activeVideoEnd) * resizeRatio;
			const double inactiveOutSize = leftPad + rightPad;
			const double inactiveOutSizeChroma = inactiveOutSize/4.0;
			
			quint16 *outY  = outputFrame.data() + (outputWidth * outputLine);
			const double yScale = Y_SCALE / yRange;
			
			for (qint32 x = 0; x < outputWidth; x++) {
				outY[x] = static_cast<quint16>(qBound(Y_MIN, ((inY[x] - yOffset) * yScale) + Y_ZERO, Y_MAX));
			}

			if(inputLine*4 < activeHeight + (videoParameters.firstActiveFrameLine * 4))
			{
				const double *inputU = componentFrame.u((inputLine) - qRound((videoParameters.firstActiveFrameLine/2.0) + (videoParameters.firstActiveFrameLine/4.0))) + qRound((videoParameters.activeVideoStart * resizeRatioChroma)/4.0);
				const double *inputV = componentFrame.v((inputLine) - qRound((videoParameters.firstActiveFrameLine/2.0) + (videoParameters.firstActiveFrameLine/4.0))) + qRound((videoParameters.activeVideoStart * resizeRatioChroma)/4.0);
				
				// Convert Y'UV to Y'CbCr [Poynton eq 25.5 p307]
				quint16 *outCB = outY + (outputWidth * (outputHeight));
				if (videoParameters.system != PAL) {
					outCB = outY + (outputWidth * (outputHeight-2));
				}
				quint16 *outCR = outCB + qRound(outputWidth * (outputHeight/4.0));

				
				const double cbScale = (C_SCALE / (ONE_MINUS_Kb * kB)) / uvRange;
				const double crScale = (C_SCALE / (ONE_MINUS_Kr * kR)) / uvRange;

				const qint32 chromaWidth = outputWidth/4.0;
				for (qint32 x = 0; x < outputWidth; x++) {
					if(x < chromaWidth)
					{
						outCB[x] = static_cast<quint16>(qBound(C_MIN, (inputU[x] * cbScale) + C_ZERO, C_MAX));
						outCR[x] = static_cast<quint16>(qBound(C_MIN, (inputV[x] * crScale) + C_ZERO, C_MAX));
					}
					else if(x < chromaWidth*2)
					{
						outCB[x] = static_cast<quint16>(qBound(C_MIN, (inputU[x+qRound(inactiveOutSizeChroma)] * cbScale) + C_ZERO, C_MAX));
						outCR[x] = static_cast<quint16>(qBound(C_MIN, (inputV[x+qRound(inactiveOutSizeChroma)] * crScale) + C_ZERO, C_MAX));
					}
					else if(x < chromaWidth*3)
					{//i dont know why we need -1 but it fix alignment
						outCB[x] = static_cast<quint16>(qBound(C_MIN, (inputU[x+qFloor(inactiveOutSizeChroma*2)] * cbScale) + C_ZERO, C_MAX));
						outCR[x] = static_cast<quint16>(qBound(C_MIN, (inputV[x+qFloor(inactiveOutSizeChroma*2)] * crScale) + C_ZERO, C_MAX));
					}
					else
					{
						outCB[x] = static_cast<quint16>(qBound(C_MIN, (inputU[x+qRound(inactiveOutSizeChroma*3)] * cbScale) + C_ZERO, C_MAX));
						outCR[x] = static_cast<quint16>(qBound(C_MIN, (inputV[x+qRound(inactiveOutSizeChroma*3)] * crScale) + C_ZERO, C_MAX));
					}

				}
				//qInfo() <<  "*1 :" << (inactiveOutSizeChroma*1) << "*2 :" << (inactiveOutSizeChroma*2) << "*3 :" << (inactiveOutSizeChroma*3) << "";
			}
            break;
        }
        case GRAY: {
            // Throw away UV and just convert Y' to the same scale as Y'CbCr
            quint16 *out = outputFrame.data() + (outputWidth * outputLine);

            const double yScale = Y_SCALE / yRange;

            for (qint32 x = 0; x < outputWidth; x++) {
                out[x] = static_cast<quint16>(qBound(Y_MIN, ((inY[x] - yOffset) * yScale) + Y_ZERO, Y_MAX));
            }

            break;
        }
    }
}
