import re

with open(r'e:\WhitegramProject\merged_settings_main.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

# 1. Find the old Appearance Meta using regex because the exact formatting might vary
old_app_pattern = r'const auto kWhitegramAppearanceMeta = BuildHelper\(\{.*?\n\}\);\n'
match = re.search(old_app_pattern, text, re.DOTALL)
if match:
    old_app = match.group(0)
    new_app = '''const auto kWhitegramStartupAnimMeta = BuildHelper({
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
    text = text.replace(old_app, new_app)
else:
    print("Could not find old appearance meta block!")

old_end_pattern = r'Type WhitegramAppearanceId\(\) \{\n\s*return WhitegramAppearance::Id\(\);\n\}\n\n\} // namespace Settings\n*'
match2 = re.search(old_end_pattern, text, re.DOTALL)
if match2:
    old_end = match2.group(0)
    new_end = '''Type WhitegramAppearanceId() { return WhitegramAppearance::Id(); }
Type WhitegramStartupAnimId() { return WhitegramStartupAnim::Id(); }

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

} // namespace Settings
'''
    text = text.replace(old_end, new_end)
else:
    print("Could not find old end block!")

with open(r'e:\WhitegramProject\Telegram\SourceFiles\ayu\ui\settings\settings_main.cpp', 'w', encoding='utf-8') as f:
    f.write(text)

print("Done writing to settings_main.cpp!")
