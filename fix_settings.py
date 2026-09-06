with open(r'e:\WhitegramProject\merged_settings_main.cpp', 'r', encoding='utf-8') as f:
    text = f.read()

correct_code = '''WhitegramTimeSpent::WhitegramTimeSpent(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramTimeSpent::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramTimeSpentMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramTimeSpentId() {
	return WhitegramTimeSpent::Id();
}

rpl::producer<QString> WhitegramMain::title() {
	return rpl::single(QString("Whitegram"));
}

WhitegramMain::WhitegramMain(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void WhitegramMain::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kWhitegramMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type WhitegramMainId() {
	return WhitegramMain::Id();
}

rpl::producer<QString> WhitegramGeneral::title() {'''

corrupt_part = '''WhitegramTimeSpent::WhitegramTimeSpent(
Type WhitegramMainId() {
	return WhitegramMain::Id();
}

rpl::producer<QString> WhitegramGeneral::title() {'''

if corrupt_part in text:
    text = text.replace(corrupt_part, correct_code)
    with open(r'e:\WhitegramProject\Telegram\SourceFiles\ayu\ui\settings\settings_main.cpp', 'w', encoding='utf-8') as f:
        f.write(text)
    print('Fixed successfully!')
else:
    print('Corrupt part not found!')
