// SPDX-FileCopyrightText: 2019-2022 Connor McLaughlin <stenzek@gmail.com>
// SPDX-License-Identifier: (GPL-3.0 OR CC-BY-NC-ND-4.0)

#pragma once
#include "util/postprocessing_shader.h"
#include <QtWidgets/QDialog>
#include <QtWidgets/QWidget>

class QGridLayout;

class PostProcessingShaderConfigWidget : public QWidget
{
  Q_OBJECT

public:
  PostProcessingShaderConfigWidget(QWidget* parent, FrontendCommon::PostProcessingShader* shader);
  ~PostProcessingShaderConfigWidget();

  QGridLayout* getLayout() { return m_layout; }

Q_SIGNALS:
  void configChanged();
  void resettingtoDefaults();

private Q_SLOTS:
  void onResetToDefaultsClicked();

protected:
  void createUi();

  FrontendCommon::PostProcessingShader* m_shader;
  QGridLayout* m_layout;
};

class PostProcessingShaderConfigDialog : public QDialog
{
  Q_OBJECT

public:
  PostProcessingShaderConfigDialog(QWidget* parent, FrontendCommon::PostProcessingShader* shader);
  ~PostProcessingShaderConfigDialog();

Q_SIGNALS:
  void configChanged();

private Q_SLOTS:
  void onConfigChanged();
  void onCloseClicked();

private:
  PostProcessingShaderConfigWidget* m_widget;
};

