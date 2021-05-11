/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/group/calls_group_large_video.h"

#include "calls/group/calls_group_members_row.h"
#include "media/view/media_view_pip.h"
#include "webrtc/webrtc_video_track.h"
#include "ui/painter.h"
#include "styles/style_calls.h"

namespace Calls::Group {
namespace {

constexpr auto kShadowMaxAlpha = 80;

} // namespace

LargeVideo::LargeVideo(
	QWidget *parent,
	const style::GroupCallLargeVideo &st,
	bool visible,
	rpl::producer<LargeVideoTrack> track,
	rpl::producer<bool> pinned)
: _content(parent, [=](QRect clip) { paint(clip); })
, _st(st)
, _pin(st::groupCallLargeVideoPin)
, _pinButton(&_content)
, _topControls(_st.controlsAlign == style::al_top) {
	_content.setVisible(visible);
	setup(std::move(track), std::move(pinned));
}

void LargeVideo::raise() {
	_content.raise();
}

void LargeVideo::setVisible(bool visible) {
	_content.setVisible(visible);
}

void LargeVideo::setGeometry(int x, int y, int width, int height) {
	_content.setGeometry(x, y, width, height);
}

rpl::producer<bool> LargeVideo::pinToggled() const {
	return _pinButton.clicks() | rpl::map([=] { return !_pinned; });
}

void LargeVideo::setup(
		rpl::producer<LargeVideoTrack> track,
		rpl::producer<bool> pinned) {
	_content.setAttribute(Qt::WA_OpaquePaintEvent);

	rpl::combine(
		_content.shownValue(),
		std::move(track)
	) | rpl::map([=](bool shown, LargeVideoTrack track) {
		return shown ? track : LargeVideoTrack();
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](LargeVideoTrack track) {
		_track = track;
		_content.update();

		_trackLifetime.destroy();
		if (!track.track) {
			return;
		}
		track.track->renderNextFrame(
		) | rpl::start_with_next([=] {
			const auto size = track.track->frameSize();
			if (size.isEmpty()) {
				track.track->markFrameShown();
			}
			_content.update();
		}, _trackLifetime);
	}, _content.lifetime());

	setupControls(std::move(pinned));
}

void LargeVideo::setupControls(rpl::producer<bool> pinned) {
	std::move(pinned) | rpl::start_with_next([=](bool pinned) {
		_pinned = pinned;
		_content.update();
	}, _content.lifetime());

	_content.sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		updateControlsGeometry();
	}, _content.lifetime());
}

void LargeVideo::updateControlsGeometry() {
	if (_topControls) {
		const auto &pin = st::groupCallLargeVideoPin.icon;
		const auto pinWidth = pin.width();
		const auto pinRight = (_content.width() - _st.pinPosition.x());
		const auto pinTop = _st.pinPosition.y();
		const auto &icon = st::groupCallLargeVideoCrossLine.icon;
		const auto iconLeft = _content.width()
			- _st.iconPosition.x()
			- icon.width();
		const auto skip = iconLeft - pinRight;
		_pinButton.setGeometry(
			pinRight - pin.width() - (skip / 2),
			0,
			pin.width() + skip,
			pinTop * 2 + pin.height());
	} else {
		_pinButton.setGeometry(
			0,
			_content.height() - _st.namePosition.y(),
			_st.namePosition.x(),
			_st.namePosition.y());
	}
}

void LargeVideo::paint(QRect clip) {
	auto p = Painter(&_content);
	const auto [image, rotation] = _track
		? _track.track->frameOriginalWithRotation()
		: std::pair<QImage, int>();
	if (image.isNull()) {
		p.fillRect(clip, Qt::black);
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	using namespace Media::View;
	const auto size = _content.size();
	const auto scaled = FlipSizeByRotation(
		image.size(),
		rotation
	).scaled(size, Qt::KeepAspectRatio);
	const auto left = (size.width() - scaled.width()) / 2;
	const auto top = (size.height() - scaled.height()) / 2;
	const auto target = QRect(QPoint(left, top), scaled);
	if (UsePainterRotation(rotation)) {
		if (rotation) {
			p.save();
			p.rotate(rotation);
		}
		p.drawImage(RotatedRect(target, rotation), image);
		if (rotation) {
			p.restore();
		}
	} else if (rotation) {
		p.drawImage(target, RotateFrameImage(image, rotation));
	} else {
		p.drawImage(target, image);
	}
	_track.track->markFrameShown();

	const auto fill = [&](QRect rect) {
		if (rect.intersects(clip)) {
			p.fillRect(rect.intersected(clip), Qt::black);
		}
	};
	if (left > 0) {
		fill({ 0, 0, left, size.height() });
	}
	if (const auto right = left + scaled.width()
		; right < size.width()) {
		fill({ right, 0, size.width() - right, size.height() });
	}
	if (top > 0) {
		fill({ 0, 0, size.width(), top });
	}
	if (const auto bottom = top + scaled.height()
		; bottom < size.height()) {
		fill({ 0, bottom, size.width(), size.height() - bottom });
	}

	paintControls(p, clip);
}

void LargeVideo::paintControls(Painter &p, QRect clip) {
	const auto shown = _controlsAnimation.value(_controlsShown ? 1. : 0.);
	if (shown == 0.) {
		return;
	}

	const auto width = _content.width();
	const auto height = _content.height();

	// Shadow.
	if (_shadow.isNull()) {
		if (_topControls) {
			_shadow = GenerateShadow(_st.shadowHeight, kShadowMaxAlpha, 0);
		} else {
			_shadow = GenerateShadow(_st.shadowHeight, 0, kShadowMaxAlpha);
		}
	}
	const auto shadowRect = QRect(
		0,
		_topControls ? 0 : (height - _st.shadowHeight),
		width,
		_st.shadowHeight);
	const auto shadowFill = shadowRect.intersected(clip);
	if (shadowFill.isEmpty()) {
		return;
	}
	const auto factor = style::DevicePixelRatio();
	p.drawImage(
		shadowFill,
		_shadow,
		QRect(
			0,
			(shadowFill.y() - shadowRect.y()) * factor,
			_shadow.width(),
			shadowFill.height() * factor));
	_track.row->lazyInitialize(st::groupCallMembersListItem);

	// Name.
	p.setPen(_topControls
		? st::groupCallVideoTextFg
		: st::groupCallVideoSubTextFg);
	const auto hasWidth = width
		- (_topControls ? _st.pinPosition.x() : _st.iconPosition.x())
		- _st.namePosition.x();
	const auto nameLeft = _st.namePosition.x();
	const auto nameTop = _topControls
		? _st.namePosition.y()
		: (height - _st.namePosition.y());
	_track.row->name().drawLeftElided(p, nameLeft, nameTop, hasWidth, width);

	// Status.
	p.setPen(st::groupCallVideoSubTextFg);
	const auto statusLeft = _st.statusPosition.x();
	const auto statusTop = _topControls
		? _st.statusPosition.y()
		: (height - _st.statusPosition.y());
	_track.row->paintComplexStatusText(
		p,
		st::groupCallLargeVideoListItem,
		statusLeft,
		statusTop,
		hasWidth,
		width,
		false,
		MembersRowStyle::LargeVideo);

	// Mute.
	const auto &icon = st::groupCallLargeVideoCrossLine.icon;
	const auto iconLeft = width - _st.iconPosition.x() - icon.width();
	const auto iconTop = _topControls
		? _st.iconPosition.y()
		: (height - _st.iconPosition.y());
	_track.row->paintMuteIcon(
		p,
		{ iconLeft, iconTop, icon.width(), icon.height() },
		MembersRowStyle::LargeVideo);

	// Pin.
	const auto pinWidth = st::groupCallLargeVideoPin.icon.width();
	const auto pinLeft = _topControls
		? (width - _st.pinPosition.x() - pinWidth)
		: _st.pinPosition.x();
	const auto pinTop = _topControls
		? _st.pinPosition.y()
		: (height - _st.pinPosition.y());
	_pin.paint(p, pinLeft, pinTop, _pinned ? 1. : 0.);
}

QImage GenerateShadow(int height, int topAlpha, int bottomAlpha) {
	Expects(topAlpha >= 0 && topAlpha < 256);
	Expects(bottomAlpha >= 0 && bottomAlpha < 256);
	Expects(height * style::DevicePixelRatio() < 65536);

	auto result = QImage(
		QSize(1, height * style::DevicePixelRatio()),
		QImage::Format_ARGB32_Premultiplied);
	if (topAlpha == bottomAlpha) {
		result.fill(QColor(0, 0, 0, topAlpha));
		return result;
	}
	constexpr auto kShift = 16;
	constexpr auto kMultiply = (1U << kShift);
	const auto values = std::abs(topAlpha - bottomAlpha);
	const auto rows = uint32(result.height());
	const auto step = (values * kMultiply) / (rows - 1);
	const auto till = rows * uint32(step);
	Assert(result.bytesPerLine() == sizeof(uint32));
	auto ints = reinterpret_cast<uint32*>(result.bits());
	if (topAlpha < bottomAlpha) {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = ((topAlpha + (i >> kShift)) << 24);
		}
	} else {
		for (auto i = uint32(0); i != till; i += step) {
			*ints++ = ((topAlpha - (i >> kShift)) << 24);
		}
	}
	return result;
}

} // namespace Calls::Group
