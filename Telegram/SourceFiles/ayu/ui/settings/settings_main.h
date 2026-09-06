// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

namespace Ui {
class GenericBox;
class VerticalLayout;
} // namespace Ui

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Settings {

class AyuMain : public Section<AyuMain> {
public:
  AyuMain(QWidget *parent, not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramMain : public Section<WhitegramMain> {
public:
  WhitegramMain(QWidget *parent,
                not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};
class WhitegramGeneral : public Section<WhitegramGeneral> {
public:
  WhitegramGeneral(QWidget *parent,
                   not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramCrypto : public Section<WhitegramCrypto> {
public:
  WhitegramCrypto(QWidget *parent,
                  not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramUpdates : public Section<WhitegramUpdates> {
public:
  WhitegramUpdates(QWidget *parent,
                   not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramTimeSpent : public Section<WhitegramTimeSpent> {
public:
  WhitegramTimeSpent(QWidget *parent,
                     not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramStartupSound : public Section<WhitegramStartupSound> {
public:
  WhitegramStartupSound(QWidget *parent,
                        not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramAppearance : public Section<WhitegramAppearance> {
public:
  WhitegramAppearance(QWidget *parent,
                      not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramMusicIsland : public Section<WhitegramMusicIsland> {
public:
  WhitegramMusicIsland(QWidget *parent,
                       not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramStartupAnim : public Section<WhitegramStartupAnim> {
public:
  WhitegramStartupAnim(QWidget *parent,
                       not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramArchives : public Section<WhitegramArchives> {
public:
  WhitegramArchives(QWidget *parent,
                    not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramBypass : public Section<WhitegramBypass> {
public:
  WhitegramBypass(QWidget *parent,
                  not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramSelfDestruct : public Section<WhitegramSelfDestruct> {
public:
  WhitegramSelfDestruct(QWidget *parent,
                        not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

class WhitegramGhostMode : public Section<WhitegramGhostMode> {
public:
  WhitegramGhostMode(QWidget *parent,
                     not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

[[nodiscard]] Type AyuMainId();
[[nodiscard]] Type WhitegramMainId();
[[nodiscard]] Type WhitegramGeneralId();
[[nodiscard]] Type WhitegramUpdatesId();
[[nodiscard]] Type WhitegramGhostModeId();
[[nodiscard]] Type WhitegramCryptoId();
[[nodiscard]] Type WhitegramTimeSpentId();
[[nodiscard]] Type WhitegramStartupSoundId();
[[nodiscard]] Type WhitegramAppearanceId();
[[nodiscard]] Type WhitegramMusicIslandId();
[[nodiscard]] Type WhitegramStartupAnimId();
[[nodiscard]] Type WhitegramArchivesId();
[[nodiscard]] Type WhitegramBypassId();
[[nodiscard]] Type WhitegramSelfDestructId();

[[nodiscard]] rpl::producer<QString> trWhitegram(const QString &ru, const QString &en);

void ShowConnectionBypassBox(not_null<Ui::GenericBox*> box);
void SetupConnectionBypass(
	not_null<Window::Controller*> controller,
	not_null<Ui::VerticalLayout*> container);

} // namespace Settings
