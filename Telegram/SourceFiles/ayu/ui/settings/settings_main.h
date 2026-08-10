// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "settings/settings_common.h"
#include "settings/settings_common_session.h"

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

class WhitegramStartupAnim : public Section<WhitegramStartupAnim> {
public:
  WhitegramStartupAnim(QWidget *parent,
                       not_null<Window::SessionController *> controller);

  [[nodiscard]] rpl::producer<QString> title() override;

private:
  void setupContent();
};

[[nodiscard]] Type AyuMainId();
[[nodiscard]] Type WhitegramMainId();
[[nodiscard]] Type WhitegramGeneralId();
[[nodiscard]] Type WhitegramCryptoId();
[[nodiscard]] Type WhitegramTimeSpentId();
[[nodiscard]] Type WhitegramStartupSoundId();
[[nodiscard]] Type WhitegramAppearanceId();
[[nodiscard]] Type WhitegramStartupAnimId();

} // namespace Settings
