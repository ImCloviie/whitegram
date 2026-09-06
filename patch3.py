import re

with open(r'e:\WhitegramProject\Telegram\SourceFiles\ayu\ui\settings\settings_main.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

meta_blocks = '''
const auto kWhitegramStartupSoundMeta = BuildHelper({
	.id = WhitegramStartupSound::Id(),
	.parentId = WhitegramGeneral::Id(),
	.title = QString::fromUtf8("Звук при запуске"),
	.icon = &st::menuIconShowAll,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	auto ayu = AyuSectionBuilder(builder);

	ayu.addSettingToggle({
		.id = u"whitegram/startupSoundEnabled"_q,
		.title = rpl::single(QString::fromUtf8("Включить звук")),
		.getter = &AyuSettings::startupSoundEnabled,
		.setter = &AyuSettings::setStartupSoundEnabled,
	});

	builder.addButton({
		.id = u"whitegram/selectStartupSound"_q,
		.title = rpl::single(QString::fromUtf8("Выбрать звук...")),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			auto audioFilters = u"Audio files (*.wav *.mp3);;"_q + FileDialog::AllFilesFilter();
			FileDialog::GetOpenPath(Core::App().getFileDialogParent(), "Open audio file", audioFilters, [=](const FileDialog::OpenResult &result) {
				if (!result.paths.isEmpty()) {
					AyuSettings::getInstance().setStartupSoundPath(result.paths.front());
				}
			});
		},
	});

	builder.addButton({
		.id = u"whitegram/resetStartupSound"_q,
		.title = rpl::single(QString::fromUtf8("Сбросить звук на стандартный")),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			AyuSettings::getInstance().setStartupSoundPath(QString::fromUtf8("C:\\Users\\ImCloviie\\Downloads\\yooooooooooo (mp3cut.net).mp3"));
		},
	});

	builder.addSkip();
});

const auto kWhitegramStartupAnimMeta = BuildHelper({
	.id = WhitegramStartupAnim::Id(),
	.parentId = WhitegramAppearance::Id(),
	.title = QString::fromUtf8("Анимация при входе"),
	.icon = &st::menuIconAnimations,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	auto ayu = AyuSectionBuilder(builder);

	ayu.addSettingToggle({
		.id = u"whitegram/startupAnimationEnabled"_q,
		.title = rpl::single(QString::fromUtf8("Стиль Steam Big Picture")),
		.getter = &AyuSettings::startupAnimationEnabled,
		.setter = &AyuSettings::setStartupAnimationEnabled,
	});

	builder.addSkip();
});

const auto kWhitegramAppearanceMeta = BuildHelper({
	.id = WhitegramAppearance::Id(),
	.parentId = WhitegramMain::Id(),
	.title = QString::fromUtf8("Оформление"),
	.icon = &st::menuIconPalette,
}, [](SectionBuilder &builder) {
	builder.addSkip();

	builder.addSectionButton({
		.title = rpl::single(QString::fromUtf8("Анимация при входе")),
		.targetSection = WhitegramStartupAnim::Id(),
		.icon = { &st::menuIconAnimations },
	});
	builder.addSkip();
});
'''

text = text.replace('const auto kWhitegramMeta = BuildHelper({', meta_blocks + '\nconst auto kWhitegramMeta = BuildHelper({')

general_btn = '''	builder.addSectionButton({
		.title = rpl::single(QString::fromUtf8("Счетчик времени")),
		.targetSection = WhitegramTimeSpent::Id(),
		.icon = { &st::menuIconInfo },
	});'''
new_general_btn = general_btn + '''
	builder.addSectionButton({
		.title = rpl::single(QString::fromUtf8("Звук при запуске")),
		.targetSection = WhitegramStartupSound::Id(),
		.icon = { &st::menuIconShowAll },
	});'''
text = text.replace(general_btn, new_general_btn)

main_btn = '''	builder.addSectionButton({
		.title = rpl::single(QString::fromUtf8("Общие")),
		.targetSection = WhitegramGeneral::Id(),
		.icon = { &st::menuIconShowAll },
	});'''
new_main_btn = main_btn + '''
	builder.addSectionButton({
		.title = rpl::single(QString::fromUtf8("Оформление")),
		.targetSection = WhitegramAppearance::Id(),
		.icon = { &st::menuIconPalette },
	});'''
if main_btn in text:
    text = text.replace(main_btn, new_main_btn)

classes = '''Type WhitegramStartupSoundId() { return WhitegramStartupSound::Id(); }
Type WhitegramAppearanceId() { return WhitegramAppearance::Id(); }
Type WhitegramStartupAnimId() { return WhitegramStartupAnim::Id(); }

WhitegramStartupSound::WhitegramStartupSound(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramStartupSound::title() {
	return rpl::single(QString::fromUtf8("Звук при запуске"));
}
void WhitegramStartupSound::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramStartupSoundMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramAppearance::WhitegramAppearance(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramAppearance::title() {
	return rpl::single(QString::fromUtf8("Оформление"));
}
void WhitegramAppearance::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramAppearanceMeta.build);
	Ui::ResizeFitChild(this, content);
}

WhitegramStartupAnim::WhitegramStartupAnim(QWidget *parent, not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}
rpl::producer<QString> WhitegramStartupAnim::title() {
	return rpl::single(QString::fromUtf8("Анимация при входе"));
}
void WhitegramStartupAnim::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramStartupAnimMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings'''
parts = text.rsplit('} // namespace Settings', 1)
text = parts[0] + classes + '\n'

with open(r'e:\WhitegramProject\Telegram\SourceFiles\ayu\ui\settings\settings_main.cpp', 'w', encoding='utf-8') as f:
    f.write(text)
print('Fixed successfully!')
