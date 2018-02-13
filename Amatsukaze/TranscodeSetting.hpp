/**
* Amtasukaze Transcode Setting
* Copyright (c) 2017-2018 Nekopanda
*
* This software is released under the MIT License.
* http://opensource.org/licenses/mit-license.php
*/
#pragma once

#include <string>
#include <direct.h>

#include "StreamUtils.hpp"

// カラースペース定義を使うため
#include "libavutil/pixfmt.h"

struct EncoderZone {
	int startFrame;
	int endFrame;
};

namespace av {

// カラースペース3セット
// x265は数値そのままでもOKだが、x264はhelpを見る限りstringでなければ
// ならないようなので変換を定義
// とりあえずARIB STD-B32 v3.7に書いてあるのだけ

// 3原色
static const char* getColorPrimStr(int color_prim) {
	switch (color_prim) {
	case AVCOL_PRI_BT709: return "bt709";
	case AVCOL_PRI_BT2020: return "bt2020";
	default:
		THROWF(FormatException,
			"Unsupported color primaries (%d)", color_prim);
	}
	return NULL;
}

// ガンマ
static const char* getTransferCharacteristicsStr(int transfer_characteritics) {
	switch (transfer_characteritics) {
	case AVCOL_TRC_BT709: return "bt709";
	case AVCOL_TRC_IEC61966_2_4: return "iec61966-2-4";
	case AVCOL_TRC_BT2020_10: return "bt2020-10";
	case AVCOL_TRC_SMPTEST2084: return "smpte-st-2084";
	case AVCOL_TRC_ARIB_STD_B67: return "arib-std-b67";
	default:
		THROWF(FormatException,
			"Unsupported color transfer characteritics (%d)", transfer_characteritics);
	}
	return NULL;
}

// 変換係数
static const char* getColorSpaceStr(int color_space) {
	switch (color_space) {
	case AVCOL_SPC_BT709: return "bt709";
	case AVCOL_SPC_BT2020_NCL: return "bt2020nc";
	default:
		THROWF(FormatException,
			"Unsupported color color space (%d)", color_space);
	}
	return NULL;
}

} // namespace av {

enum ENUM_ENCODER {
	ENCODER_X264,
	ENCODER_X265,
	ENCODER_QSVENC,
	ENCODER_NVENC,
};

enum ENUM_FORMAT {
	FORMAT_MP4,
	FORMAT_MKV
};

struct BitrateSetting {
	double a, b;
	double h264;
	double h265;

	double getTargetBitrate(VIDEO_STREAM_FORMAT format, double srcBitrate) const {
		double base = a * srcBitrate + b;
		if (format == VS_H264) {
			return base * h264;
		}
		else if (format == VS_H265) {
			return base * h265;
		}
		return base;
	}
};

static const char* encoderToString(ENUM_ENCODER encoder) {
	switch (encoder) {
	case ENCODER_X264: return "x264";
	case ENCODER_X265: return "x265";
	case ENCODER_QSVENC: return "QSVEnc";
	case ENCODER_NVENC: return "NVEnc";
	}
	return "Unknown";
}

static std::string makeEncoderArgs(
	ENUM_ENCODER encoder,
	const std::string& binpath,
	const std::string& options,
	const VideoFormat& fmt,
	const std::string& outpath)
{
	StringBuilder sb;

	sb.append("\"%s\"", binpath);

	// y4mヘッダにあるので必要ない
	//ss << " --fps " << fmt.frameRateNum << "/" << fmt.frameRateDenom;
	//ss << " --input-res " << fmt.width << "x" << fmt.height;
	//ss << " --sar " << fmt.sarWidth << ":" << fmt.sarHeight;

	if (fmt.colorPrimaries != AVCOL_PRI_UNSPECIFIED) {
		sb.append(" --colorprim %s", av::getColorPrimStr(fmt.colorPrimaries));
	}
	if (fmt.transferCharacteristics != AVCOL_TRC_UNSPECIFIED) {
		sb.append(" --transfer %s", av::getTransferCharacteristicsStr(fmt.transferCharacteristics));
	}
	if (fmt.colorSpace != AVCOL_TRC_UNSPECIFIED) {
		sb.append(" --colormatrix %s", av::getColorSpaceStr(fmt.colorSpace));
	}

	// インターレース
	switch (encoder) {
	case ENCODER_X264:
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		sb.append(fmt.progressive ? "" : " --tff");
		break;
	case ENCODER_X265:
		sb.append(fmt.progressive ? " --no-interlace" : " --interlace tff");
		break;
	}

	sb.append(" %s -o \"%s\"", options, outpath);

	// 入力形式
	switch (encoder) {
	case ENCODER_X264:
		sb.append(" --stitchable")
			.append(" --demuxer y4m -");
		break;
	case ENCODER_X265:
		sb.append(" --no-opt-qp-pps --no-opt-ref-list-length-pps")
			.append(" --y4m --input -");
		break;
	case ENCODER_QSVENC:
	case ENCODER_NVENC:
		sb.append(" --format raw --y4m -i -");
		break;
	}

	return sb.str();
}

static std::vector<std::string> makeMuxerArgs(
	ENUM_FORMAT format,
	const std::string& binpath,
	const std::string& timelineeditorpath,
	const std::string& mp4boxpath,
	const std::string& inVideo,
	const VideoFormat& videoFormat,
	const std::vector<std::string>& inAudios,
	const std::string& outpath,
	const std::string& tmpoutpath,
	const std::string& chapterpath,
	const std::string& timecodepath,
	std::pair<int, int> timebase,
	const std::vector<std::string>& inSubs,
	const std::vector<std::string>& subsTitles)
{
	std::vector<std::string> ret;

	StringBuilder sb;
	sb.append("\"%s\"", binpath);

	if (format == FORMAT_MP4) {
		bool needChapter = (chapterpath.size() > 0);
		bool needTimecode = (timecodepath.size() > 0);
		bool needSubs = (inSubs.size() > 0);

		// まずはmuxerで映像、音声、チャプターをmux
		if (videoFormat.fixedFrameRate) {
			sb.append(" -i \"%s?fps=%d/%d\"", inVideo,
				videoFormat.frameRateNum, videoFormat.frameRateDenom);
		}
		else {
			sb.append(" -i \"%s\"", inVideo);
		}
		for (const auto& inAudio : inAudios) {
			sb.append(" -i \"%s\"", inAudio);
		}
		// timelineeditorがチャプターを消すのでtimecodeがある時はmp4boxで入れる
		if (needChapter && !needTimecode) {
			sb.append(" --chapter \"%s\"", chapterpath);
			needChapter = false;
		}
		sb.append(" --optimize-pd");

		std::string dst = needTimecode ? tmpoutpath : outpath;
		sb.append(" -o \"%s\"", dst);

		ret.push_back(sb.str());
		sb.clear();

		if (needTimecode) {
			// 必要ならtimelineeditorでtimecodeを埋め込む
			sb.append("\"%s\"", timelineeditorpath)
				.append(" --track 1")
				.append(" --timecode \"%s\"", timecodepath)
				.append(" --media-timescale %d", timebase.first)
				.append(" --media-timebase %d", timebase.second)
				.append(" \"%s\"", dst)
				.append(" \"%s\"", outpath);
			ret.push_back(sb.str());
			sb.clear();
			needTimecode = false;
		}

		if (needChapter || needSubs) {
			// 字幕とチャプターを埋め込む
			sb.append("\"%s\"", mp4boxpath);
			for (int i = 0; i < (int)inSubs.size(); ++i) {
				if (subsTitles[i] == "SRT") { // mp4はSRTのみ
					sb.append(" -add \"%s#:name=%s\"", inSubs[i], subsTitles[i]);
				}
			}
			needSubs = false;
			// timecodeがある場合はこっちでチャプターを入れる
			if (needChapter) {
				sb.append(" -chap \"%s\"", chapterpath);
				needChapter = false;
			}
			sb.append(" \"%s\"", outpath);
			ret.push_back(sb.str());
			sb.clear();
		}
	}
	else { // mkv

		if (chapterpath.size() > 0) {
			sb.append(" --chapters \"%s\"", chapterpath);
		}

		sb.append(" -o \"%s\"", outpath);

		if (timecodepath.size()) {
			sb.append(" --timestamps \"0:%s\"", timecodepath);
		}
		sb.append(" \"%s\"", inVideo);

		for (const auto& inAudio : inAudios) {
			sb.append(" \"%s\"", inAudio);
		}
		for (int i = 0; i < (int)inSubs.size(); ++i) {
			sb.append(" --track-name \"0:%s\" \"%s\"", subsTitles[i], inSubs[i]);
		}

		ret.push_back(sb.str());
		sb.clear();
	}

	return ret;
}

static std::string makeTimelineEditorArgs(
	const std::string& binpath,
	const std::string& inpath,
	const std::string& outpath,
	const std::string& timecodepath)
{
	StringBuilder sb;
	sb.append("\"%s\"", binpath)
		.append(" --track 1")
		.append(" --timecode \"%s\"", timecodepath)
		.append(" \"%s\"", inpath)
		.append(" \"%s\"", outpath);
	return sb.str();
}

static const char* cmOutMaskToString(int outmask) {
	switch (outmask)
	{
	case 1: return "通常";
	case 2: return "CMをカット";
	case 3: return "通常出力とCMカット出力";
	case 4: return "CMのみ";
	case 5: return "通常出力とCM出力";
	case 6: return "本編とCMを分離";
	case 7: return "通常,本編,CM全出力";
	}
	return "不明";
}

inline bool ends_with(std::string const & value, std::string const & ending)
{
	if (ending.size() > value.size()) return false;
	return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

enum AMT_CLI_MODE {
	AMT_CLI_TS,
	AMT_CLI_GENERIC,
};

class TempDirectory : AMTObject, NonCopyable
{
public:
	TempDirectory(AMTContext& ctx, const std::string& tmpdir)
		: AMTObject(ctx)
	{
		if (tmpdir.size() == 0) {
			// 指定がなければ作らない
			return;
		}
		for (int code = (int)time(NULL) & 0xFFFFFF; code > 0; ++code) {
			auto path = genPath(tmpdir, code);
			if (_mkdir(path.c_str()) == 0) {
				path_ = path;
				break;
			}
			if (errno != EEXIST) {
				break;
			}
		}
		if (path_.size() == 0) {
			THROW(IOException, "一時ディレクトリ作成失敗");
		}
		
		std::string abolutePath;
		int sz = GetFullPathNameA(path_.c_str(), 0, 0, 0);
		abolutePath.resize(sz);
		GetFullPathNameA(path_.c_str(), sz, &abolutePath[0], 0);
		abolutePath.resize(sz - 1);
		path_ = abolutePath;
	}
	~TempDirectory() {
		if (path_.size() == 0) {
			return;
		}
		// 一時ファイルを削除
		ctx.clearTmpFiles();
		// ディレクトリ削除
		if (_rmdir(path_.c_str()) != 0) {
			ctx.warn("一時ディレクトリ削除に失敗: ", path_.c_str());
		}
	}

	std::string path() const {
		if (path_.size() == 0) {
			THROW(RuntimeException, "一時フォルダの指定がありません");
		}
		return path_;
	}

private:
	std::string path_;

	std::string genPath(const std::string& base, int code)
	{
		return StringFormat("%s/amt%d", base, code);
	}
};

static const char* GetCMSuffix(CMType cmtype) {
	switch (cmtype) {
	case CMTYPE_CM: return "-cm";
	case CMTYPE_NONCM: return "-main";
	case CMTYPE_BOTH: return "";
	}
	return "";
}

struct Config {
	// 一時フォルダ
	std::string workDir;
	std::string mode;
	std::string modeArgs; // テスト用
	// 入力ファイルパス（拡張子を含む）
	std::string srcFilePath;
	// 出力ファイルパス（拡張子を除く）
	std::string outVideoPath;
	// 結果情報JSON出力パス
	std::string outInfoJsonPath;
	// DRCSマッピングファイルパス
	std::string drcsMapPath;
	std::string drcsOutPath;
	// フィルタパス
	std::string filterScriptPath;
	std::string postFilterScriptPath;
	// エンコーダ設定
	ENUM_ENCODER encoder;
	std::string encoderPath;
	std::string encoderOptions;
	std::string muxerPath;
	std::string timelineditorPath;
	std::string mp4boxPath;
	ENUM_FORMAT format;
	bool splitSub;
	bool twoPass;
	bool autoBitrate;
	bool chapter;
	bool subtitles;
	BitrateSetting bitrate;
	double bitrateCM;
	int serviceId;
	DecoderSetting decoderSetting;
	// CM解析用設定
	std::vector<std::string> logoPath;
	bool ignoreNoLogo;
	bool ignoreNoDrcsMap;
	bool noDelogo;
	std::string chapterExePath;
	std::string joinLogoScpPath;
	std::string joinLogoScpCmdPath;
	std::string joinLogoScpOptions;
	int cmoutmask;
	// デバッグ用設定
	bool dumpStreamInfo;
	bool systemAvsPlugin;
};

class ConfigWrapper : public AMTObject
{
public:
	ConfigWrapper(
		AMTContext& ctx,
		const Config& conf)
		: AMTObject(ctx)
		, conf(conf)
		, tmpDir(ctx, conf.workDir)
	{
		for (int cmtypei = 0; cmtypei < CMTYPE_MAX; ++cmtypei) {
			if (conf.cmoutmask & (1 << cmtypei)) {
				cmtypes.push_back((CMType)cmtypei);
			}
		}
	}

	std::string getMode() const {
		return conf.mode;
	}

	std::string getModeArgs() const {
		return conf.modeArgs;
	}

	std::string getSrcFilePath() const {
		return conf.srcFilePath;
	}

	std::string getOutInfoJsonPath() const {
		return conf.outInfoJsonPath;
	}

	std::string getFilterScriptPath() const {
		return conf.filterScriptPath;
	}

	std::string getPostFilterScriptPath() const {
		return conf.postFilterScriptPath;
	}

	ENUM_ENCODER getEncoder() const {
		return conf.encoder;
	}

	std::string getEncoderPath() const {
		return conf.encoderPath;
	}

	std::string getEncoderOptions() const {
		return conf.encoderOptions;
	}

	ENUM_FORMAT getFormat() const {
		return conf.format;
	}

	std::string getMuxerPath() const {
		return conf.muxerPath;
	}

	std::string getTimelineEditorPath() const {
		return conf.timelineditorPath;
	}

	std::string getMp4BoxPath() const {
		return conf.mp4boxPath;
	}

	bool isSplitSub() const {
		return conf.splitSub;
	}

	bool isTwoPass() const {
		return conf.twoPass;
	}

	bool isAutoBitrate() const {
		return conf.autoBitrate;
	}

	bool isChapterEnabled() const {
		return conf.chapter;
	}

	bool isSubtitlesEnabled() const {
		return conf.subtitles;
	}

	BitrateSetting getBitrate() const {
		return conf.bitrate;
	}

	double getBitrateCM() const {
		return conf.bitrateCM;
	}

	int getServiceId() const {
		return conf.serviceId;
	}

	DecoderSetting getDecoderSetting() const {
		return conf.decoderSetting;
	}

	const std::vector<std::string>& getLogoPath() const {
		return conf.logoPath;
	}

	bool isIgnoreNoLogo() const {
		return conf.ignoreNoLogo;
	}

	bool isIgnoreNoDrcsMap() const {
		return conf.ignoreNoDrcsMap;
	}

	bool isNoDelogo() const {
		return conf.noDelogo;
	}

	std::string getChapterExePath() const {
		return conf.chapterExePath;
	}

	std::string getJoinLogoScpPath() const {
		return conf.joinLogoScpPath;
	}

	std::string getJoinLogoScpCmdPath() const {
		return conf.joinLogoScpCmdPath;
	}

	std::string getJoinLogoScpOptions() const {
		return conf.joinLogoScpOptions;
	}

	const std::vector<CMType>& getCMTypes() const {
		return cmtypes;
	}

	bool isDumpStreamInfo() const {
		return conf.dumpStreamInfo;
	}

	bool isSystemAvsPlugin() const {
		return conf.systemAvsPlugin;
	}

	std::string getAudioFilePath() const {
		return regtmp(StringFormat("%s/audio.dat", tmpDir.path()));
	}

	std::string getWaveFilePath() const {
		return regtmp(StringFormat("%s/audio.wav", tmpDir.path()));
	}

	std::string getIntVideoFilePath(int index) const {
		return regtmp(StringFormat("%s/i%d.mpg", tmpDir.path(), index));
	}

	std::string getStreamInfoPath() const {
		return conf.outVideoPath + "-streaminfo.dat";
	}

	std::string getEncVideoFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/v%d-%d%s.raw", tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	std::string getTimecodeFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/v%d-%d%s.timecode.txt", tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	std::string getEncStatsFilePath(int vindex, int index, CMType cmtype) const
	{
		auto str = StringFormat("%s/s%d-%d%s.log", tmpDir.path(), vindex, index, GetCMSuffix(cmtype));
		ctx.registerTmpFile(str);
		// x264は.mbtreeも生成するので
		ctx.registerTmpFile(str + ".mbtree");
		// x265は.cutreeも生成するので
		ctx.registerTmpFile(str + ".cutree");
		return str;
	}

	std::string getIntAudioFilePath(int vindex, int index, int aindex, CMType cmtype) const {
		return regtmp(StringFormat("%s/a%d-%d-%d%s.aac",
			tmpDir.path(), vindex, index, aindex, GetCMSuffix(cmtype)));
	}

	std::string getTmpASSFilePath(int vindex, int index, int langindex, CMType cmtype) const {
		return regtmp(StringFormat("%s/c%d-%d-%d%s.ass",
			tmpDir.path(), vindex, index, langindex, GetCMSuffix(cmtype)));
	}

	std::string getTmpSRTFilePath(int vindex, int index, int langindex, CMType cmtype) const {
		return regtmp(StringFormat("%s/c%d-%d-%d%s.srt",
			tmpDir.path(), vindex, index, langindex, GetCMSuffix(cmtype)));
	}

	std::string getLogoTmpFilePath() const {
		return regtmp(StringFormat("%s/logotmp.dat", tmpDir.path()));
	}

	std::string getTmpAMTSourcePath(int vindex) const {
		return regtmp(StringFormat("%s/amts%d.dat", tmpDir.path(), vindex));
	}

	std::string getTmpSourceAVSPath(int vindex) const {
		return regtmp(StringFormat("%s/amts%d.avs", tmpDir.path(), vindex));
	}

	std::string getTmpLogoFramePath(int vindex) const {
		return regtmp(StringFormat("%s/logof%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterExePath(int vindex) const {
		return regtmp(StringFormat("%s/chapter_exe%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterExeOutPath(int vindex) const {
		return regtmp(StringFormat("%s/chapter_exe_o%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpTrimAVSPath(int vindex) const {
		return regtmp(StringFormat("%s/trim%d.avs", tmpDir.path(), vindex));
	}

	std::string getTmpJlsPath(int vindex) const {
		return regtmp(StringFormat("%s/jls%d.txt", tmpDir.path(), vindex));
	}

	std::string getTmpChapterPath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/chapter%d-%d%s.txt",
			tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	std::string getVfrTmpFilePath(int vindex, int index, CMType cmtype) const {
		return regtmp(StringFormat("%s/t%d-%d%s.mp4", tmpDir.path(), vindex, index, GetCMSuffix(cmtype)));
	}

	const char* getOutputExtention() const {
		switch (conf.format) {
		case FORMAT_MP4: return "mp4";
		case FORMAT_MKV: return "mkv";
		}
		return "amatsukze";
	}

	std::string getOutFilePath(int index, CMType cmtype) const {
		StringBuilder sb;
		sb.append("%s", conf.outVideoPath);
		if (index != 0) {
			sb.append("-%d", index);
		}
		sb.append("%s.%s", GetCMSuffix(cmtype), getOutputExtention());
		return sb.str();
	}

	std::string getOutSummaryPath() const {
		return StringFormat("%s.txt", conf.outVideoPath);
	}

	std::string getDRCSMapPath() const {
		return conf.drcsMapPath;
	}

	std::string getDRCSOutPath(const std::string& md5) const {
		return StringFormat("%s\\%s.bmp", conf.drcsOutPath, md5);
	}

	std::string getOptions(
		VIDEO_STREAM_FORMAT srcFormat, double srcBitrate, bool pulldown,
		int pass, const std::vector<EncoderZone>& zones, int vindex, int index, CMType cmtype) const
	{
		StringBuilder sb;
		sb.append("%s", conf.encoderOptions);
		if (conf.autoBitrate) {
			double targetBitrate = conf.bitrate.getTargetBitrate(srcFormat, srcBitrate);
			double maxBitrate = std::max(targetBitrate * 2, srcBitrate);
			if (cmtype == CMTYPE_CM) {
				targetBitrate *= conf.bitrateCM;
			}
			if (conf.encoder == ENCODER_QSVENC) {
				sb.append(" --la %d --maxbitrate %d", (int)targetBitrate, (int)maxBitrate);
			}
			else if (conf.encoder == ENCODER_NVENC) {
				sb.append(" --vbrhq %d --maxbitrate %d", (int)targetBitrate, (int)maxBitrate);
			}
			else {
				sb.append(" --bitrate %d --vbv-maxrate %d --vbv-bufsize %d", 
					(int)targetBitrate, (int)maxBitrate, (int)maxBitrate);
			}
		}
		if (pass >= 0) {
			sb.append(" --pass %d --stats \"%s\"",
				pass, getEncStatsFilePath(vindex, index, cmtype));
		}
		if (zones.size() && conf.bitrateCM != 1.0 && conf.encoder != ENCODER_QSVENC && conf.encoder != ENCODER_NVENC) {
			sb.append(" --zones ");
			for (int i = 0; i < (int)zones.size(); ++i) {
				auto zone = zones[i];
				sb.append("%s%d,%d,b=%3g", (i > 0) ? "/" : "", zone.startFrame, zone.endFrame, conf.bitrateCM);
			}
		}
		return sb.str();
	}

	void dump() const {
		ctx.info("[設定]");
		if (conf.mode != "ts") {
			ctx.info("Mode: %s", conf.mode.c_str());
		}
		ctx.info("入力: %s", conf.srcFilePath.c_str());
		ctx.info("出力: %s", conf.outVideoPath.c_str());
		ctx.info("一時フォルダ: %s", tmpDir.path().c_str());
		ctx.info("出力フォーマット: %s", formatToString(conf.format));
		ctx.info("エンコーダ: %s (%s)", conf.encoderPath.c_str(), encoderToString(conf.encoder));
		ctx.info("エンコーダオプション: %s", conf.encoderOptions.c_str());
		if (conf.autoBitrate) {
			ctx.info("自動ビットレート: 有効 (%g:%g:%g)", 
				conf.bitrate.a, conf.bitrate.b, conf.bitrate.h264);
		}
		else {
			ctx.info("自動ビットレート: 無効");
		}
		ctx.info("エンコード/出力: %s/%s",
			conf.twoPass ? "2パス" : "1パス",
			cmOutMaskToString(conf.cmoutmask));
		ctx.info("チャプター解析: %s%s",
			conf.chapter ? "有効" : "無効",
			(conf.chapter && conf.ignoreNoLogo) ? "" : "（ロゴ必須）");
		if (conf.chapter) {
			for (int i = 0; i < (int)conf.logoPath.size(); ++i) {
				ctx.info("logo%d: %s", (i + 1), conf.logoPath[i].c_str());
			}
			ctx.info("ロゴ消し: %s", conf.noDelogo ? "しない" : "する");
		}
		ctx.info("字幕: %s", conf.subtitles ? "有効" : "無効");
		if (conf.subtitles) {
			ctx.info("DRCSマッピング: %s", conf.drcsMapPath.c_str());
		}
		if (conf.serviceId > 0) {
			ctx.info("サービスID: %d", conf.serviceId);
		}
		else {
			ctx.info("サービスID: 指定なし");
		}
		ctx.info("デコーダ: MPEG2:%s H264:%s",
			decoderToString(conf.decoderSetting.mpeg2),
			decoderToString(conf.decoderSetting.h264));
	}

private:
	Config conf;
	TempDirectory tmpDir;
	std::vector<CMType> cmtypes;

	const char* decoderToString(DECODER_TYPE decoder) const {
		switch (decoder) {
		case DECODER_QSV: return "QSV";
		case DECODER_CUVID: return "CUVID";
		}
		return "default";
	}

	const char* formatToString(ENUM_FORMAT fmt) const {
		switch (fmt) {
		case FORMAT_MP4: return "MP4";
		case FORMAT_MKV: return "Matroska";
		}
		return "unknown";
	}

	std::string regtmp(std::string str) const {
		ctx.registerTmpFile(str);
		return str;
	}
};

