// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/plugin_info_box.h"

#include "apiwrap.h"
#include "core/file_utilities.h"
#include "core/ui_integration.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/stickers/data_stickers_set.h"
#include "lang/lang_keys.h"
#include "chat_helpers/stickers_lottie.h"
#include "ayu/features/whitegram_plugins/whitegram_plugins.h"
#include "ayu/features/whitegram_plugins/whitegram_plugins_ui.h"
#include "history/view/media/history_view_sticker_player.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_premium.h"
#include "ui/painter.h"
#include "ui/rect.h"
#include "ui/vertical_list.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_session_controller.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainterPath>
#include <QRegularExpression>
#include <array>


namespace Ui {
namespace {

QString ExtractField(const QByteArray &data, const QString &key) {
	// supports probably all kinds of weird python syntax + both " and '
	// "description"
	// """description"""
	// (
	// 	"description\n\n"
	// 	"description\n"
	// )
	static const auto quotedValue = QRegularExpression(
		uR"field((?:"""((?:\\[\s\S]|(?!""")[^\\])*)"""|'''((?:\\[\s\S]|(?!''')[^\\])*)'''|(?!""")"((?:\\.|[^\r\n"\\])*)"|(?!''')'((?:\\.|[^\r\n'\\])*)'))field"_q);
	const auto re = QRegularExpression(
		uR"(__%1__\s*=\s*(?:%2|\((?:\s*%2)+\s*\)))"_q.arg(
			key,
			quotedValue.pattern()));
	const auto match = re.match(QString::fromUtf8(data));
	if (!match.hasMatch()) {
		return {};
	}
	auto result = QString();
	auto parts = quotedValue.globalMatch(match.captured());
	while (parts.hasNext()) {
		const auto partMatch = parts.next();
		auto part = partMatch.captured(1);
		for (auto i = 2
			; part.isNull() && i <= quotedValue.captureCount()
			; ++i) {
			part = partMatch.captured(i);
		}
		part.replace(u"\\n"_q, u"\n"_q);
		part.replace(u"\\t"_q, u"\t"_q);
		part.replace(u"\\\""_q, u"\""_q);
		part.replace(u"\\'"_q, u"'"_q);
		part.replace(u"\\\\"_q, u"\\"_q);
		result += part;
	}
	return result;
}

TextWithEntities ParsePluginDescriptionPlain(const QString &text) {
	auto result = TextWithEntities{ text };
	TextUtilities::ParseEntities(
		result,
		TextParseLinks | TextParseMentions);
	return result;
}

TextWithEntities ParsePluginDescriptionLinks(const QString &text) {
	static const auto linkExpression = QRegularExpression(
		uR"(\[([^\]\r\n]+)\]\(([^)\r\n]+)\))"_q);
	auto result = TextWithEntities();
	auto offset = 0;
	auto matches = linkExpression.globalMatch(text);
	while (matches.hasNext()) {
		const auto match = matches.next();
		const auto url = match.capturedView(2);
		if (!Ui::InputField::IsValidMarkdownLink(url)) {
			continue;
		}
		result.append(ParsePluginDescriptionPlain(
			text.mid(offset, match.capturedStart() - offset)));
		result.append(Ui::Text::Link(
			match.captured(1),
			url.toString()));
		offset = match.capturedEnd();
	}
	result.append(ParsePluginDescriptionPlain(text.mid(offset)));
	return result;
}

TextWithEntities ParsePluginDescription(const QString &text) {
	static const auto boldExpression = QRegularExpression(
		uR"(\*\*(.+?)\*\*)"_q,
		QRegularExpression::DotMatchesEverythingOption);
	auto result = TextWithEntities();
	auto offset = 0;
	auto matches = boldExpression.globalMatch(text);
	while (matches.hasNext()) {
		const auto match = matches.next();
		result.append(ParsePluginDescriptionLinks(
			text.mid(offset, match.capturedStart() - offset)));
		result.append(Ui::Text::Wrapped(
			ParsePluginDescriptionLinks(match.captured(1)),
			EntityType::Bold));
		offset = match.capturedEnd();
	}
	result.append(ParsePluginDescriptionLinks(text.mid(offset)));
	return result;
}

void FillPluginInfoBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	const QString &pluginPath,
	const PluginMetadata& metadata,
	DocumentData *stickerDoc) {
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	box->setWidth(st::wgPluginInstallWidth);
	box->verticalLayout()->resizeToWidth(box->width());
	const auto isRu = Lang::GetInstance().id().startsWith(u"ru"_q)
		|| Lang::GetInstance().baseId().startsWith(u"ru"_q);
	const auto engineReady = Whitegram::Plugins::Started();
	const auto sourceInfo = Whitegram::Plugins::Call(u"metadata"_q, QJsonObject{
		{ u"path"_q, pluginPath },
	}).toObject();
	const auto trusted = sourceInfo.value(u"trusted"_q).toBool();
	auto installedVersion = QString();
	auto installed = false;
	for (const auto &value : Whitegram::Plugins::Call(u"list"_q).toArray()) {
		const auto info = value.toObject();
		if (info.value(u"id"_q).toString() == metadata.id) {
			installed = true;
			installedVersion = info.value(u"version"_q).toString();
			break;
		}
	}


	box->addTopButton(st::pluginShowInFolder,
					  [=]
					  {
						  File::ShowInFolder(pluginPath);
					  });

	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());
	Ui::AddSkip(box->verticalLayout());

	if (stickerDoc) {
		const auto stickerSize = st::wgPluginInstallIconSize;
		const auto radius = stickerSize / 5;
		const auto widget = box->verticalLayout()->add(
			object_ptr<Ui::RpWidget>(box->verticalLayout()));
		widget->resize(Size(stickerSize));

		const auto media = stickerDoc->createMediaView();
		media->checkStickerLarge();
		const auto sticker = stickerDoc->sticker();

		const auto player = widget->lifetime().make_state<
			std::unique_ptr<HistoryView::StickerPlayer>>();

		const auto createPlayer = [=] {
			if (sticker->isLottie()) {
				*player = std::make_unique<HistoryView::LottiePlayer>(
					ChatHelpers::LottiePlayerFromDocument(
						media.get(),
						ChatHelpers::StickerLottieSize::StickerSet,
						Size(stickerSize),
						Lottie::Quality::High));
			} else if (sticker->isWebm()) {
				*player = std::make_unique<HistoryView::WebmPlayer>(
					media->owner()->location(),
					media->bytes(),
					Size(stickerSize));
			} else {
				*player = std::make_unique<HistoryView::StaticStickerPlayer>(
					media->owner()->location(),
					media->bytes(),
					Size(stickerSize));
			}
			(*player)->setRepaintCallback([=] { widget->update(); });
		};

		if (media->loaded()) {
			createPlayer();
		} else {
			rpl::single() | rpl::then(
				stickerDoc->owner().session().downloaderTaskFinished()
			) | rpl::filter([=] {
				return media->loaded();
			}) | rpl::take(1) | rpl::on_next([=] {
				createPlayer();
			}, widget->lifetime());
		}

		widget->paintRequest(
		) | rpl::on_next([=] {
			if (!*player || !(*player)->ready()) {
				return;
			}
			const auto frame = (*player)->frame(
				Size(stickerSize),
				QColor(0, 0, 0, 0),
				false,
				crl::now(),
				false);
			if (frame.image.isNull()) {
				return;
			}
			auto p = QPainter(widget);
			PainterHighQualityEnabler hq(p);
			const auto x = (widget->width() - stickerSize) / 2;
			QPainterPath path;
			path.addRoundedRect(
				QRectF(x, 0, stickerSize, stickerSize),
				radius,
				radius);
			p.setClipPath(path);
			p.drawImage(
				QRect(x, 0, stickerSize, stickerSize),
				frame.image);
			(*player)->markFrameShown();
		}, widget->lifetime());

		Ui::AddSkip(box->verticalLayout());
		Ui::AddSkip(box->verticalLayout());
	}

	if (!stickerDoc) {
		const auto size = st::wgPluginInstallIconSize;
		static const auto colors = std::array {
			&st::historyPeer1UserpicBg,
			&st::historyPeer2UserpicBg,
			&st::historyPeer3UserpicBg,
			&st::historyPeer4UserpicBg,
			&st::historyPeer5UserpicBg,
			&st::historyPeer6UserpicBg,
		};
		const auto colorIndex = qHash(metadata.id) % colors.size();
		const auto icon = box->addRow(object_ptr<Ui::FixedHeightWidget>(box, size));
		icon->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(icon);
			PainterHighQualityEnabler hq(p);
			const auto rect = QRect((icon->width() - size) / 2, 0, size, size);
			p.setPen(Qt::NoPen);
			p.setBrush((*colors[colorIndex])->c);
			p.drawRoundedRect(rect, size / 4., size / 4.);
			auto font = QFont(st::normalFont);
			font.setPixelSize(size / 2);
			font.setBold(true);
			p.setFont(font);
			p.setPen(Qt::white);
			p.drawText(rect, Qt::AlignCenter, metadata.name.left(1).toUpper());
		}, icon->lifetime());
		Ui::AddSkip(box->verticalLayout(), st::wgPluginCardPadding);
	}

	box->verticalLayout()->add(
		object_ptr<Ui::FlatLabel>(
			box->verticalLayout(),
			rpl::single(metadata.name),
			st::wgPluginInstallTitle),
		st::boxRowPadding,
		style::al_top);

	{
		const auto versionLabel = box->verticalLayout()->add(
			object_ptr<Ui::FlatLabel>(
				box->verticalLayout(),
				st::wgPluginInstallMeta),
			st::boxRowPadding,
			style::al_justify);

		const auto versionPrefix = tr::ayu_PluginVersion(tr::now)
			+ u" "_q
			+ (installed && !installedVersion.isEmpty()
				? installedVersion + u" → "_q : QString())
			+ metadata.version;

		if (metadata.author.isEmpty()) {
			versionLabel->setText(
				versionPrefix
				+ u" \u2022 "_q
				+ tr::ayu_PluginNoAuthor(tr::now));
		} else {
			auto text = TextWithEntities{
				versionPrefix + u" \u2022 "_q,
			};
			static const auto usernameRe = QRegularExpression(
				u"@([A-Za-z0-9_]+)"_q);
			const auto &author = metadata.author;
			auto it = usernameRe.globalMatch(author);
			auto lastEnd = 0;
			while (it.hasNext()) {
				const auto match = it.next();
				if (match.capturedStart() > lastEnd) {
					text.append(TextWithEntities{
						author.mid(lastEnd,
								   match.capturedStart() - lastEnd)
					});
				}
				text.append(Ui::Text::Link(
					match.captured(0),
					controller->session().createInternalLinkFull(
						match.captured(1))));
				lastEnd = match.capturedEnd();
			}
			if (lastEnd < author.size()) {
				text.append(TextWithEntities{
					author.mid(lastEnd)
				});
			}
			versionLabel->setMarkedText(
				text,
				Core::TextContext({
					.session = &controller->session(),
				}));
		}
	}

	Ui::AddSkip(box->verticalLayout(), st::wgPluginIconGap);
	const auto sourceText = trusted
		? (isRu ? u"Доверенный плагин"_q : u"Trusted plugin"_q)
		: (isRu ? u"Источник не проверен"_q : u"Unverified source"_q);
	const auto pill = box->addRow(object_ptr<Ui::FixedHeightWidget>(box, st::wgPluginAction.height));
	pill->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(pill);
		PainterHighQualityEnabler hq(p);
		const auto color = trusted ? st::boxTextFgGood->c : st::windowSubTextFg->c;
		auto background = color;
		background.setAlphaF(0.14);
		const auto width = st::normalFont->width(sourceText) + 2 * st::wgPluginCardPadding;
		const auto rect = QRect((pill->width() - width) / 2, 0, width, pill->height());
		p.setPen(Qt::NoPen);
		p.setBrush(background);
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		p.setFont(st::normalFont);
		p.setPen(color);
		p.drawText(rect, Qt::AlignCenter, sourceText);
	}, pill->lifetime());
	Ui::AddSkip(box->verticalLayout(), st::wgPluginCardPadding);

	const auto descriptionCard = box->addRow(
		object_ptr<Ui::VerticalLayout>(box), st::boxRowPadding);
	descriptionCard->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(descriptionCard);
		PainterHighQualityEnabler hq(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver->c);
		p.drawRoundedRect(descriptionCard->rect(), st::wgPluginCardRadius, st::wgPluginCardRadius);
	}, descriptionCard->lifetime());
	const auto description = descriptionCard->add(
		object_ptr<Ui::FlatLabel>(descriptionCard, st::boxLabel),
		style::margins(st::wgPluginCardPadding, st::wgPluginCardPadding,
			st::wgPluginCardPadding, st::wgPluginCardPadding));
	description->setMarkedText(metadata.description.isEmpty()
		? TextWithEntities{tr::ayu_PluginNoDescription(tr::now)}
		: ParsePluginDescription(metadata.description),
		Core::TextContext({ .session = &controller->session() }));
	description->setSelectable(true);
	Ui::AddSkip(box->verticalLayout(), st::wgPluginIconGap);

	if (!metadata.requirements.isEmpty()) {
		const auto chipsContainer = box->verticalLayout()->add(
			object_ptr<Ui::RpWidget>(box->verticalLayout()),
			st::boxRowPadding);

		auto chips = std::vector<Ui::RoundButton*>();
		for (const auto &dep : metadata.requirements) {
			const auto chip = Ui::CreateChild<Ui::RoundButton>(
				chipsContainer,
				rpl::single(dep),
				st::defaultTableSmallButton);
			chip->setTextTransform(
				Ui::RoundButtonTextTransform::NoTransform);
			chip->setClickedCallback([dep] {
				File::OpenUrl(
					u"https://pypi.org/project/"_q + dep + u"/"_q);
			});
			chips.push_back(chip);
		}

		const auto chipsPtr = chipsContainer->lifetime().make_state<
			std::vector<Ui::RoundButton*>>(std::move(chips));

		chipsContainer->sizeValue(
		) | rpl::on_next([=](QSize size)
						 {
							 const auto skip = st::pluginChipSkip;
							 auto x = 0;
							 auto y = 0;
							 auto widthLeft = size.width();
							 auto firstRow = true;
							 for (const auto chip : *chipsPtr) {
								 const auto chipWidth = chip->width();
								 const auto chipHeight = chip->height();
								 if (!firstRow && chipWidth > widthLeft) {
									 x = 0;
									 y += chipHeight + skip;
									 widthLeft = size.width();
								 }
								 firstRow = false;
								 chip->moveToLeft(x, y);
								 x += chipWidth + skip;
								 widthLeft = size.width() - x;
							 }
							 if (!chipsPtr->empty()) {
								 const auto lastChip = chipsPtr->back();
								 chipsContainer->resize(
									 size.width(),
									 lastChip->y() + lastChip->height());
							 }
						 },
						 chipsContainer->lifetime());

		Ui::AddSkip(box->verticalLayout());
	}

	Ui::AddSkip(box->verticalLayout());

	if (!engineReady) {
		box->addRow(object_ptr<Ui::FlatLabel>(box,
			rpl::single(isRu ? u"Включите движок в настройках плагинов, чтобы продолжить установку."_q
				: u"Enable the plugin engine in settings to continue installing."_q),
			st::boxDividerLabel));
	}
	const auto footer = box->setPinnedToBottomContent(object_ptr<Ui::VerticalLayout>(box));
	const auto addAction = [&](QString text, const style::RoundButton &style, Fn<void()> callback) {
		const auto button = footer->add(object_ptr<Ui::RoundButton>(footer,
			rpl::single(std::move(text)), style), st::boxRowPadding);
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		button->setClickedCallback(std::move(callback));
		footer->widthValue() | rpl::on_next([=](int width) {
			button->setFullWidth(width - st::boxRowPadding.left() - st::boxRowPadding.right());
		}, button->lifetime());
	};
	Ui::AddSkip(footer, st::wgPluginIconGap);
	if (engineReady) {
		addAction(installed ? (isRu ? u"Обновить"_q : u"Update"_q)
			: (isRu ? u"Установить"_q : u"Install"_q), st::wgPluginInstallButton, [=] {
			Whitegram::Plugins::InstallPluginFromPath(controller, pluginPath);
			box->closeBox();
		});
	}
	addAction(isRu ? u"Отмена"_q : u"Cancel"_q, st::wgPluginCancelButton, [=] {
		box->closeBox();
	});
	Ui::AddSkip(footer, st::wgPluginIconGap);

}

}

PluginMetadata ParsePluginMetadata(const QByteArray &data) {
	auto metadata = PluginMetadata();

	metadata.id = ExtractField(data, u"id"_q);
	metadata.name = ExtractField(data, u"name"_q).left(100);
	metadata.description = ExtractField(data, u"description"_q).left(8000);
	metadata.author = ExtractField(data, u"author"_q).left(200);
	metadata.version = ExtractField(data, u"version"_q).left(32);
	metadata.icon = ExtractField(data, u"icon"_q);
	metadata.minVersion = ExtractField(data, u"min_version"_q);

	const auto reqString = ExtractField(data, u"requirements"_q);
	if (!reqString.isEmpty()) {
		for (const auto &part : reqString.split(',')) {
			const auto trimmed = part.trimmed();
			if (!trimmed.isEmpty()) {
				metadata.requirements.append(trimmed);
			}
		}
	}

	if (metadata.version.isEmpty()) {
		metadata.version = u"1.0"_q;
	}

	static const auto idRegex = QRegularExpression(
		u"^[a-zA-Z][a-zA-Z0-9_-]{1,31}$"_q);
	if (metadata.id.isEmpty()
		|| !idRegex.match(metadata.id).hasMatch()) {
		return {};
	}

	if (metadata.name.isEmpty()) {
		return {};
	}

	return metadata;
}

void ShowPluginInfoBox(
	not_null<Window::SessionController*> controller,
	const QString &pluginPath,
	PluginMetadata metadata) {
	const auto showBox = [controller, pluginPath](
		PluginMetadata md,
		DocumentData *stickerDoc)
	{
		controller->show(Box(
			FillPluginInfoBox,
			controller,
			pluginPath,
			std::move(md),
			stickerDoc));
	};

	if (metadata.icon.isEmpty()) {
		showBox(std::move(metadata), nullptr);
		return;
	}

	const auto parts = metadata.icon.split('/');
	if (parts.size() != 2) {
		showBox(std::move(metadata), nullptr);
		return;
	}

	const auto& shortName = parts[0];
	const auto index = parts[1].toInt();
	const auto session = &controller->session();
	const auto shared = std::make_shared<PluginMetadata>(
		std::move(metadata));

	session->api().request(MTPmessages_GetStickerSet(
		Data::InputStickerSet({
			.shortName = shortName,
		}),
		MTP_int(0)
	)).done([=](const MTPmessages_StickerSet &result)
	{
		auto doc = (DocumentData*) nullptr;
		result.match([&](const MTPDmessages_stickerSet &data)
					 {
						 const auto &v = data.vdocuments().v;
						 if (index >= 0 && index < v.size()) {
							 doc = session->data().processDocument(v[index]);
						 }
					 },
					 [](const MTPDmessages_stickerSetNotModified &)
					 {
					 });

		if (!doc) {
			showBox(std::move(*shared), nullptr);
			return;
		}

		const auto media = doc->createMediaView();
		media->checkStickerLarge();

		if (media->loaded()) {
			showBox(std::move(*shared), doc);
			return;
		}

		rpl::single() | rpl::then(
			session->downloaderTaskFinished()
		) | rpl::filter([=] {
			return media->loaded();
		}) | rpl::take(1) | rpl::on_next([=] {
			showBox(std::move(*shared), doc);
		}, session->lifetime());
		media->automaticLoad(doc->stickerSetOrigin(), nullptr);
	}).fail([=](const MTP::Error &)
	{
		showBox(std::move(*shared), nullptr);
	}).send();
}

}
