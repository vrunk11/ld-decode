/************************************************************************

    main.cpp

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2018-2020 Simon Inns
    Copyright (C) 2019-2022 Adam Sampson
    Copyright (C) 2021 Chad Page
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

#include <QCoreApplication>
#include <QDebug>
#include <QtGlobal>
#include <QCommandLineParser>
#include <QThread>
#include <fstream>
#include <memory>

#include "decoderpool.h"
#include "lddecodemetadata.h"
#include "logging.h"

#include "comb.h"
#include "monodecoder.h"
#include "ntscdecoder.h"
#include "outputwriter.h"
#include "palcolour.h"
#include "paldecoder.h"
#include "transformpal.h"

// Load the thresholds file for the Transform decoders, if specified. We must
// do this after PalColour has been configured, so we know how many values to
// expect.
//
// Return true on success; on failure, print a message and return false.
static bool loadTransformThresholds(QCommandLineParser &parser, QCommandLineOption &transformThresholdsOption, PalColour::Configuration &palConfig)
{
    if (!parser.isSet(transformThresholdsOption)) {
        // Nothing to load
        return true;
    }

    // Open the file
    QString filename = parser.value(transformThresholdsOption);
    std::ifstream thresholdsFile(filename.toStdString());
    if (thresholdsFile.fail()) {
        qCritical() << "Transform thresholds file could not be opened:" << filename;
        return false;
    }

    // Read threshold values from the file
    palConfig.transformThresholds.clear();
    while (true) {
        double value;
        thresholdsFile >> value;
        if (thresholdsFile.eof()) {
            break;
        }
        if (value < 0.0 || value > 1.0) {
            qCritical() << "Values in Transform thresholds file must be between 0 and 1:" << filename;
            return false;
        }
        if (thresholdsFile.fail()) {
            qCritical() << "Couldn't parse Transform thresholds file:" << filename;
            return false;
        }
        palConfig.transformThresholds.push_back(value);
    }

    // Check we've read the right number
    if (palConfig.transformThresholds.size() != palConfig.getThresholdsSize()) {
        qCritical() << "Transform thresholds file contained" << palConfig.transformThresholds.size()
                    << "values, expecting" << palConfig.getThresholdsSize() << "values:" << filename;
        return false;
    }

    thresholdsFile.close();
    return true;
}

int main(int argc, char *argv[])
{
    //set 'binary mode' for stdin and stdout on windows
    setBinaryMode();
    // Install the local debug message handler
    setDebug(true);
    qInstallMessageHandler(debugOutputHandler);

    QCoreApplication a(argc, argv);

    // Set application name and version
    QCoreApplication::setApplicationName("ld-chroma-decoder");
    QCoreApplication::setApplicationVersion(QString("Branch: %1 / Commit: %2").arg(APP_BRANCH, APP_COMMIT));
    QCoreApplication::setOrganizationDomain("domesday86.com");

    // Set up the command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription(
                "ld-chroma-decoder - Colourisation filter for ld-decode\n"
                "\n"
                "(c)2018-2020 Simon Inns\n"
                "(c)2019-2021 Adam Sampson\n"
                "(c)2018-2021 Chad Page\n"
                "(c)2021 Phillip Blucas\n"
                "Contains PALcolour: Copyright (c)2018 William Andrew Steer\n"
                "Contains Transform PAL: Copyright (c)2014 Jim Easterbrook\n"
                "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode");
    parser.addHelpOption();
    parser.addVersionOption();

    // -- General options --

    // Add the standard debug options --debug and --quiet
    addStandardDebugOptions(parser);

    // Option to specify a different JSON input file
    QCommandLineOption inputJsonOption(QStringList() << "input-json",
                                       QCoreApplication::translate("main", "Specify the input JSON file (default input.json)"),
                                       QCoreApplication::translate("main", "filename"));
    parser.addOption(inputJsonOption);
	
	// Option to specify a second .tbc file containing the chroma
    QCommandLineOption chromaInputOption(QStringList() << "c" << "chroma" << "chroma-input",
                                       QCoreApplication::translate("main", "Specify chroma input TBC file"),
                                       QCoreApplication::translate("main", "filename"));
    parser.addOption(chromaInputOption);
	
	// Option to avoid using chroma file and process cvbs only
    QCommandLineOption cvbsOnlyOption(QStringList() << "cvbs",
                                       QCoreApplication::translate("main", "Treat input as cvbs only"));
    parser.addOption(cvbsOnlyOption);

    // Option to select start frame (sequential) (-s)
    QCommandLineOption startFrameOption(QStringList() << "s" << "start",
                                        QCoreApplication::translate("main", "Specify the start frame number"),
                                        QCoreApplication::translate("main", "number"));
    parser.addOption(startFrameOption);

    // Option to select length (-l)
    QCommandLineOption lengthOption(QStringList() << "l" << "length",
                                        QCoreApplication::translate("main", "Specify the length (number of frames to process)"),
                                        QCoreApplication::translate("main", "number"));
    parser.addOption(lengthOption);

    // Option to reverse the field order (-r)
    QCommandLineOption setReverseOption(QStringList() << "r" << "reverse",
                                       QCoreApplication::translate("main", "Reverse the field order to second/first (default first/second)"));
    parser.addOption(setReverseOption);

    // Option to specify chroma gain
    QCommandLineOption chromaGainOption(QStringList() << "chroma-gain",
                                        QCoreApplication::translate("main", "Gain factor applied to chroma components (default 1.0)"),
                                        QCoreApplication::translate("main", "number"));
    parser.addOption(chromaGainOption);

    // Option to specify chroma phase
    QCommandLineOption chromaPhaseOption(QStringList() << "chroma-phase",
                                        QCoreApplication::translate("main", "Phase rotation applied to chroma components (degrees; default 0.0)"),
                                        QCoreApplication::translate("main", "number"));
    parser.addOption(chromaPhaseOption);

    // Option to select the output format (-p)
    QCommandLineOption outputFormatOption(QStringList() << "p" << "output-format",
                                       QCoreApplication::translate("main", "Output format (rgb24, rgb48, yuv444p, yuv444p16, yuv422p, yuv422p16, yuv411p, gray16, gray8; default rgb); RGB48, YUV444, YUV444P16, YUV422P, YUV422P16, YUV411P, GRAY16, GRAY8 pixel formats are supported"),
                                       QCoreApplication::translate("main", "output-format"));
    parser.addOption(outputFormatOption);
	
	// Option to select header
    QCommandLineOption headerOption(QStringList() << "header",
                                       QCoreApplication::translate("main", "header format (raw, y4m, mkv, nut; default raw)"),
                                       QCoreApplication::translate("main", "header"));
    parser.addOption(headerOption);
	
	// Option to set the black and white output flag (causes output to be black and white) (-b)
    QCommandLineOption setFFV1Option(QStringList() << "ffv1" << "FFV1",
                                       QCoreApplication::translate("main", "Encode video to FFV1 for NUT header (option always on for MKV)"));
    parser.addOption(setFFV1Option);

    // Option to set the black and white output flag (causes output to be black and white) (-b)
    QCommandLineOption setBwModeOption(QStringList() << "b" << "blackandwhite",
                                       QCoreApplication::translate("main", "Output in black and white"));
    parser.addOption(setBwModeOption);
	
	// Option to select resizing format
    QCommandLineOption outputResampleOption(QStringList() << "size",
                                       QCoreApplication::translate("main", "Select output size : (square,tvl,native,dv,pixel) (default : native)"),
                                       QCoreApplication::translate("main", "size-format"));
    parser.addOption(outputResampleOption);
	
	// Option to select a size value
    QCommandLineOption outputResampleValueOption(QStringList() << "size-v" << "size-value",
                                       QCoreApplication::translate("main", "Select size value : (quantity in tvl/pixel depending on format)"),
                                       QCoreApplication::translate("main", "number"));
    parser.addOption(outputResampleValueOption);
	
    // Option to select output padding (-pad)
    QCommandLineOption outputPaddingOption(QStringList() << "pad" << "output-padding",
                                       QCoreApplication::translate("main", "Pad the output frame to a multiple of this many pixels on both axes (1 means no padding, maximum is 32)"),
                                       QCoreApplication::translate("main", "number"));
    parser.addOption(outputPaddingOption);

    // Option to select which decoder to use (-f)
    QCommandLineOption decoderOption(QStringList() << "f" << "decoder",
                                     QCoreApplication::translate("main", "Decoder to use (pal2d, transform2d, transform3d, ntsc1d, ntsc2d, ntsc3d, ntsc3dnoadapt, mono; default automatic)"),
                                     QCoreApplication::translate("main", "decoder"));
    parser.addOption(decoderOption);

    // Option to select the number of threads (-t)
    QCommandLineOption threadsOption(QStringList() << "t" << "threads",
                                     QCoreApplication::translate("main", "Specify the number of concurrent threads (default number of logical CPUs)"),
                                     QCoreApplication::translate("main", "number"));
    parser.addOption(threadsOption);

    // Option to override calculated firstActiveFieldLine in our video parameters (-ffll)
    QCommandLineOption firstFieldLineOption(QStringList() << "ffll" << "first_active_field_line",
                                            QCoreApplication::translate("main", "The first visible line of a field. Range 1-259 for NTSC (default: 20), 2-308 for PAL (default: 22)"),
                                            QCoreApplication::translate("main", "number"));
    parser.addOption(firstFieldLineOption);

    // Option to override calculated lastActiveFieldLine in our video parameters (-lfll)
    QCommandLineOption lastFieldLineOption(QStringList() << "lfll" << "last_active_field_line",
                                           QCoreApplication::translate("main", "The last visible line of a field. Range 1-259 for NTSC (default: 259), 2-308 for PAL (default: 308)"),
                                           QCoreApplication::translate("main", "number"));
    parser.addOption(lastFieldLineOption);

    // Option to override calculated firstActiveFrameLine in our video parameters (-ffrl)
    QCommandLineOption firstFrameLineOption(QStringList() << "ffrl" << "first_active_frame_line",
                                            QCoreApplication::translate("main", "The first visible line of a frame. Range 1-525 for NTSC (default: 40), 1-620 for PAL (default: 44)"),
                                            QCoreApplication::translate("main", "number"));
    parser.addOption(firstFrameLineOption);

    // Option to override calculated lastActiveFieldLine in our video parameters (-lfll)
    QCommandLineOption lastFrameLineOption(QStringList() << "lfrl" << "last_active_frame_line",
                                           QCoreApplication::translate("main", "The last visible line of a frame. Range 1-525 for NTSC (default: 525), 1-620 for PAL (default: 620)"),
                                           QCoreApplication::translate("main", "number"));
    parser.addOption(lastFrameLineOption);

    // -- NTSC decoder options --

    // Option to overlay the adaptive filter map
    QCommandLineOption showMapOption(QStringList() << "o" << "oftest",
                                     QCoreApplication::translate("main", "NTSC: Overlay the adaptive filter map (only used for testing)"));
    parser.addOption(showMapOption);

    // Option to set the chroma noise reduction level
    QCommandLineOption chromaNROption(QStringList() << "chroma-nr",
                                      QCoreApplication::translate("main", "NTSC: Chroma noise reduction level in dB (default 0.0)"),
                                      QCoreApplication::translate("main", "number"));
    parser.addOption(chromaNROption);

    // Option to set the luma noise reduction level
    QCommandLineOption lumaNROption(QStringList() << "luma-nr",
                                    QCoreApplication::translate("main", "Luma noise reduction level in dB (default 0.0)"),
                                    QCoreApplication::translate("main", "number"));
    parser.addOption(lumaNROption);

    // Option to use phase compensating decoder
    QCommandLineOption ntscPhaseCompOption(QStringList() << "ntsc-phase-comp",
                                           QCoreApplication::translate("main", "NTSC: Adjust phase per-line using burst phase"));
    parser.addOption(ntscPhaseCompOption);

    // -- PAL decoder options --

    // Option to use Simple PAL UV filter
    QCommandLineOption simplePALOption(QStringList() << "simple-pal",
                                           QCoreApplication::translate("main", "Transform: Use 1D UV filter (default 2D)"));
    parser.addOption(simplePALOption);

    // Option to select the Transform PAL threshold
    QCommandLineOption transformThresholdOption(QStringList() << "transform-threshold",
                                                QCoreApplication::translate("main", "Transform: Uniform similarity threshold (default 0.4)"),
                                                QCoreApplication::translate("main", "number"));
    parser.addOption(transformThresholdOption);

    // Option to select the Transform PAL thresholds file
    QCommandLineOption transformThresholdsOption(QStringList() << "transform-thresholds",
                                                 QCoreApplication::translate("main", "Transform: File containing per-bin similarity thresholds"),
                                                 QCoreApplication::translate("main", "file"));
    parser.addOption(transformThresholdsOption);

    // Option to overlay the FFTs
    QCommandLineOption showFFTsOption(QStringList() << "show-ffts",
                                      QCoreApplication::translate("main", "Transform: Overlay the input and output FFTs"));
    parser.addOption(showFFTsOption);

    // -- Positional arguments --

    // Positional argument to specify input video file
    parser.addPositionalArgument("input", QCoreApplication::translate("main", "Specify input TBC file (- for piped input)"));

    // Positional argument to specify output video file
    parser.addPositionalArgument("output", QCoreApplication::translate("main", "Specify output file (omit or - for piped output)"));

    // Process the command line options and arguments given by the user
    parser.process(a);

    // Standard logging options
    processStandardDebugOptions(parser);

    // Get the arguments from the parser
    QString inputFileName;
    QString outputFileName = "-";
    QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.count() == 2) {
        inputFileName = positionalArguments.at(0);
        outputFileName = positionalArguments.at(1);
    } else if (positionalArguments.count() == 1) {
        inputFileName = positionalArguments.at(0);
    } else {
        // Quit with error
        qCritical("You must specify the input TBC and output files");
        return -1;
    }
	
	//chroma source
	QString chromaFileName = inputFileName;
	chromaFileName.chop(4);
	chromaFileName += "_chroma.tbc";
	if(!parser.isSet(cvbsOnlyOption))
	{
		if (parser.isSet(chromaInputOption)) {
			chromaFileName = parser.value(chromaInputOption);
		}
		
		if(!QFile::exists(chromaFileName))
		{
			chromaFileName = "";
			qInfo("No chroma file found source is assumed to be CVBS");
		}
		else
		{
			qInfo("Chroma file found source is assumed to be Y/C");
		}
	}
	else
	{
		chromaFileName = "";
		qInfo("Source is assumed to be CVBS");
	}


    // Check filename arguments are reasonable
    if (inputFileName == "-" && !parser.isSet(inputJsonOption)) {
        // Quit with error
        qCritical("With piped input, you must also specify the input JSON file");
        return -1;
    }
    if (inputFileName == outputFileName && outputFileName != "-") {
        // Quit with error
        qCritical("Input and output files cannot be the same");
        return -1;
    }

    qint32 startFrame = -1;
    qint32 length = -1;
    qint32 maxThreads = QThread::idealThreadCount();
    PalColour::Configuration palConfig;
    Comb::Configuration combConfig;
    MonoDecoder::MonoConfiguration monoConfig;
    OutputWriter::Configuration outputConfig;

    if (parser.isSet(startFrameOption)) {
        startFrame = parser.value(startFrameOption).toInt();

        if (startFrame < 1) {
            // Quit with error
            qCritical("Specified startFrame must be at least 1");
            return -1;
        }
    }

    if (parser.isSet(lengthOption)) {
        length = parser.value(lengthOption).toInt();

        if (length < 1) {
            // Quit with error
            qCritical("Specified length must be greater than zero frames");
            return -1;
        }
    }

    if (parser.isSet(threadsOption)) {
        maxThreads = parser.value(threadsOption).toInt();

        if (maxThreads < 1) {
            // Quit with error
            qCritical("Specified number of threads must be greater than zero");
            return -1;
        }
    }

    if (parser.isSet(chromaGainOption)) {
        const double value = parser.value(chromaGainOption).toDouble();
        palConfig.chromaGain = value;
        combConfig.chromaGain = value;

        if (value < 0.0) {
            // Quit with error
            qCritical("Chroma gain must not be less than 0");
            return -1;
        }
    }

    if (parser.isSet(chromaPhaseOption)) {
        const double value = parser.value(chromaPhaseOption).toDouble();
        palConfig.chromaPhase = value;
        combConfig.chromaPhase = value;
    }

    bool bwMode = parser.isSet(setBwModeOption);
    if (bwMode) {
        palConfig.chromaGain = 0.0;
        combConfig.chromaGain = 0.0;
    }

    if (parser.isSet(showMapOption)) {
        combConfig.showMap = true;
    }

    if (parser.isSet(chromaNROption)) {
        combConfig.cNRLevel = parser.value(chromaNROption).toDouble();

        if (combConfig.cNRLevel < 0.0) {
            // Quit with error
            qCritical("Chroma noise reduction cannot be negative");
            return -1;
        }
    }

    if (parser.isSet(lumaNROption)) {
		if(chromaFileName == "")
		{
			combConfig.yNRLevel = parser.value(lumaNROption).toDouble();
			palConfig.yNRLevel = parser.value(lumaNROption).toDouble();
		}
		else
		{
			combConfig.yNRLevel = 0.0;
			palConfig.yNRLevel = 0.0;
		}
        monoConfig.yNRLevel = parser.value(lumaNROption).toDouble();

        if (combConfig.yNRLevel < 0.0) {
            // Quit with error
            qCritical("Luma noise reduction cannot be negative");
            return -1;
        }
    }

    if (parser.isSet(ntscPhaseCompOption)) {
        combConfig.phaseCompensation = true;
    }

    if (parser.isSet(simplePALOption)) {
        palConfig.simplePAL = true;
    }

    if (parser.isSet(transformThresholdOption)) {
        palConfig.transformThreshold = parser.value(transformThresholdOption).toDouble();

        if (palConfig.transformThreshold < 0.0 || palConfig.transformThreshold > 1.0) {
            // Quit with error
            qCritical("Transform threshold must be between 0 and 1");
            return -1;
        }
    }

    LdDecodeMetaData::LineParameters lineParameters;
    if (parser.isSet(firstFieldLineOption)) {
        lineParameters.firstActiveFieldLine = parser.value(firstFieldLineOption).toInt();
    }
    
    if (parser.isSet(lastFieldLineOption)) {
        lineParameters.lastActiveFieldLine = parser.value(lastFieldLineOption).toInt();
    }
    
    if (parser.isSet(firstFrameLineOption)) {
        lineParameters.firstActiveFrameLine = parser.value(firstFrameLineOption).toInt();
    }
    
    if (parser.isSet(lastFrameLineOption)) {
        lineParameters.lastActiveFrameLine = parser.value(lastFrameLineOption).toInt();
    }

    // Work out the metadata filename
    QString inputJsonFileName = inputFileName + ".json";
    if (parser.isSet(inputJsonOption)) {
        inputJsonFileName = parser.value(inputJsonOption);
    }

    // Load the source video metadata
    LdDecodeMetaData metaData;
    if (!metaData.read(inputJsonFileName)) {
        qInfo() << "Unable to open ld-decode metadata file";
        return -1;
    }
    
    metaData.processLineParameters(lineParameters);
    
    // Reverse field order if required
    if (parser.isSet(setReverseOption)) {
        qInfo() << "Expected field order is reversed to second field/first field";
        metaData.setIsFirstFieldFirst(false);
    }

    // Work out which decoder to use
    QString decoderName;
    if (parser.isSet(decoderOption)) {
        decoderName = parser.value(decoderOption);
    } else if (metaData.getVideoParameters().system == NTSC) {
        decoderName = "ntsc2d";
    } else {
        decoderName = "pal2d";
    }

    // Require ntsc3d if the map overlay is selected
    if (combConfig.showMap && decoderName != "ntsc3d") {
        qCritical() << "Can only show adaptive filter map with the ntsc3d decoder";
        return -1;
    }

    // Require transform2d/3d if the FFT overlay is selected
    if (palConfig.showFFTs && decoderName != "transform2d" && decoderName != "transform3d") {
        qCritical() << "Can only show FFTs with the transform2d/transform3d decoders";
        return -1;
    }

    // Select the decoder
    std::unique_ptr<Decoder> videoDecoder;
    std::unique_ptr<Decoder> lumaDecoder;
    if (decoderName == "pal2d") {
        videoDecoder = std::make_unique<PalDecoder>(palConfig);
    } else if (decoderName == "transform2d") {
        palConfig.chromaFilter = PalColour::transform2DFilter;
        if (!loadTransformThresholds(parser, transformThresholdsOption, palConfig)) {
            return -1;
        }
        videoDecoder = std::make_unique<PalDecoder>(palConfig);
    } else if (decoderName == "transform3d") {
        palConfig.chromaFilter = PalColour::transform3DFilter;
        if (!loadTransformThresholds(parser, transformThresholdsOption, palConfig)) {
            return -1;
        }
        videoDecoder = std::make_unique<PalDecoder>(palConfig);
    } else if (decoderName == "ntsc1d") {
        combConfig.dimensions = 1;
        videoDecoder = std::make_unique<NtscDecoder>(combConfig);
    } else if (decoderName == "ntsc2d") {
        combConfig.dimensions = 2;
        videoDecoder = std::make_unique<NtscDecoder>(combConfig);
    } else if (decoderName == "ntsc3d") {
        combConfig.dimensions = 3;
        videoDecoder = std::make_unique<NtscDecoder>(combConfig);
    } else if (decoderName == "ntsc3dnoadapt") {
        combConfig.dimensions = 3;
        combConfig.adaptive = false;
        videoDecoder = std::make_unique<NtscDecoder>(combConfig);
    } else if (decoderName == "mono") {
        videoDecoder = std::make_unique<MonoDecoder>(monoConfig);
		//remove chroma file to process only the luma
		chromaFileName = "";
    } else {
        qCritical() << "Unknown decoder" << decoderName;
        return -1;
    }
	
	//select header
	if (parser.isSet(headerOption))
	{
		QString headerName = parser.value(headerOption);
		if (headerName == "y4m") {
			outputConfig.outputHeader = "y4m";
			outputConfig.useOutputHeader = true;
		}
		else if (headerName == "mkv")
		{
			outputConfig.outputHeader = "mkv";
			outputConfig.useOutputHeader = true;
			outputConfig.useFFV1 = true;
		}
		else if (headerName == "nut")
		{
			outputConfig.outputHeader = "nut";
			outputConfig.useOutputHeader = true;
			outputConfig.useFFV1 = parser.isSet(setFFV1Option);
		}
		else if (headerName == "raw")
		{
			outputConfig.outputHeader = "raw";
			outputConfig.useOutputHeader = false;
		}
		else {
			qCritical() << "Unknown header" << headerName;
			return -1;
		}
	}

    // Select the output format
    QString outputFormatName;
    if (parser.isSet(outputFormatOption)) {
        outputFormatName = parser.value(outputFormatOption);
    } else {
		if (outputConfig.outputHeader == "y4m")
		{
			outputFormatName = "yuv444p";
		}
		else
		{
			outputFormatName = "rgb";
		}
    }
    if (outputFormatName == "yuv" || outputFormatName == "yuv444p" || outputFormatName == "yuv444p16" || outputFormatName == "yuv422p" || outputFormatName == "yuv422p16" || outputFormatName == "yuv411p" || outputFormatName == "y16" || outputFormatName == "y8" || outputFormatName == "y4m") { // keep yuv and y4m option as legacy
        if (outputFormatName == "y4m") {
            outputConfig.useOutputHeader = true;
			outputConfig.outputHeader = "y4m";
        }
        if (bwMode || decoderName == "mono" || outputFormatName == "gray16" || outputFormatName == "gray8" || outputFormatName == "y16" || outputFormatName == "y8") {
            outputConfig.pixelFormat = OutputWriter::PixelFormat::GRAY;
        } else {
			if (outputFormatName == "yuv422p" || outputFormatName == "yuv422p16")
			{
				outputConfig.pixelFormat = OutputWriter::PixelFormat::YUV422;
			}
			else if (outputFormatName == "yuv411p")
			{
				outputConfig.pixelFormat = OutputWriter::PixelFormat::YUV411;
			}
			else // yuv444
			{
				outputConfig.pixelFormat = OutputWriter::PixelFormat::YUV444;
			}
        }
    } else if (outputFormatName == "rgb" || outputFormatName == "rgb24" || outputFormatName == "rgb48") {//keep rgb as legacy
        outputConfig.pixelFormat = OutputWriter::PixelFormat::RGB;
    } else {
        qCritical() << "Unknown output format" << outputFormatName;
        return -1;
    }
	
	if(outputFormatName == "rgb24" || outputFormatName == "yuv444p" || outputFormatName == "yuv422p" || outputFormatName == "yuv411p" || outputFormatName == "gray8" || outputFormatName == "y8")
	{
		outputConfig.is8bit = true;
	}

    if (parser.isSet(outputPaddingOption)) {
        outputConfig.paddingAmount = parser.value(outputPaddingOption).toInt();
        if (outputConfig.paddingAmount < 1 || outputConfig.paddingAmount > 32) {
            qInfo() << "Invalid value" << outputConfig.paddingAmount << "specified for padding amount, defaulting to 8.";
            outputConfig.paddingAmount = 8;
        }
    }
	
	if (parser.isSet(outputResampleOption))
	{
		outputConfig.useResampling = true;
		QString sizeFormatName = parser.value(outputResampleOption);
		if(sizeFormatName == "square")
		{
			if(metaData.getVideoParameters().system == NTSC)
			{
				if(metaData.getVideoParameters().isWidescreen)
				{
					outputConfig.resampleWidth = 864;
				}
				else
				{
					outputConfig.resampleWidth = 648;
				}
			}
			else
			{
				if(metaData.getVideoParameters().isWidescreen)
				{
					//adjust for chroma subsampling
					if (outputFormatName == "yuv422")
					{
						outputConfig.resampleWidth = 1029;
					}
					else if (outputFormatName == "yuv411")
					{
						outputConfig.resampleWidth = 1028;
					}
					else
					{
						outputConfig.resampleWidth = 1030;
					}
				}
				else
				{
					//adjust for chroma subsampling
					if (outputFormatName == "yuv422" || outputFormatName == "yuv411")
					{
						outputConfig.resampleWidth = 772;
					}
					else
					{
						outputConfig.resampleWidth = 773;
					}
				}
			}
		}
		else if((sizeFormatName == "custom" || sizeFormatName == "pixel" || sizeFormatName == "px") && parser.isSet(outputResampleValueOption))
		{
			if (parser.value(outputResampleValueOption).toInt() < 1) {
            // Quit with error
            qCritical("Specified size must be greater than zero");
            return -1;
			}
			if ((parser.value(outputResampleValueOption).toInt() % 4 != 0) && outputFormatName == "yuv411") {
            // Quit with error
            qCritical("Specified size must be a multiple of 4 when using yuv411");
            return -1;
			}
			if ((parser.value(outputResampleValueOption).toInt() % 2 != 0) && outputFormatName == "yuv411") {
            // Quit with error
            qCritical("Specified size must be a multiple of 2 when using yuv422");
            return -1;
			}
			outputConfig.resampleWidth = parser.value(outputResampleValueOption).toInt();
		}
		else if(sizeFormatName == "tvl" && parser.isSet(outputResampleValueOption))
		{
			if (parser.value(outputResampleValueOption).toInt() < 1) {
            // Quit with error
            qCritical("Specified size must be greater than zero");
            return -1;
			}
			if(metaData.getVideoParameters().isWidescreen)
			{
				outputConfig.resampleWidth = qRound(parser.value(outputResampleValueOption).toInt()*1.7777);
			}
			else
			{
				outputConfig.resampleWidth = qRound(parser.value(outputResampleValueOption).toInt()*1.3333);
			}
			
			if(outputFormatName == "yuv422" && outputConfig.resampleWidth)
			{
				const double denominator = ((outputConfig.resampleWidth/2.0) - qFloor(outputConfig.resampleWidth/2.0));
				if(denominator != 0)
				{
					outputConfig.resampleWidth += 1;
					qInfo() << "Size adjusted to fit chroma subsampling";
				}
			}
			else if(outputFormatName == "yuv411")
			{
				const double denominator = ((outputConfig.resampleWidth/4.0) - qFloor(outputConfig.resampleWidth/4.0));
				if(denominator != 0)
				{
					outputConfig.resampleWidth = outputConfig.resampleWidth + 4 - (denominator*4);
					qInfo() << "Size adjusted to fit chroma subsampling";
				}
			}
		}
		else if((sizeFormatName == "dv" || sizeFormatName == "DV") && parser.isSet(outputResampleOption))
		{
			outputConfig.resampleWidth = 720;
		}
		else
		{
			outputConfig.resampleWidth = metaData.getVideoParameters().activeVideoEnd - metaData.getVideoParameters().activeVideoStart;
		}
	}
	else
	{
		//enable resampling anyway for resampling the chroma
		if(outputFormatName == "yuv422" || outputFormatName == "yuv411")
		{
			outputConfig.resampleWidth = metaData.getVideoParameters().activeVideoEnd - metaData.getVideoParameters().activeVideoStart;
			outputConfig.useResampling = true;
		}
		else
		{
			outputConfig.useResampling = false;
		}
	}
	
	lumaDecoder = std::make_unique<MonoDecoder>(monoConfig);
	if(chromaFileName != "")
	{
		// Perform the processing
		DecoderPool decoderPool(*lumaDecoder, *videoDecoder, inputFileName, chromaFileName, metaData, outputConfig, outputFileName, startFrame, length, maxThreads);
		if (!decoderPool.process()) {
			return -1;
		}
	}
	else
	{
		// Perform the processing ,luma decoder is not used
		DecoderPool decoderPool(*videoDecoder, *lumaDecoder, inputFileName, chromaFileName, metaData, outputConfig, outputFileName, startFrame, length, maxThreads);
		if (!decoderPool.process()) {
			return -1;
		}
	}

    // Quit with success
    return 0;
}
