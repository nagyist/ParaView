// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqImageCompressorWidget.h"
#include "ui_pqImageCompressorWidget.h"

#include "pqPropertiesPanel.h"
#include "pqProxyWidget.h"
#include "pqWidgetUtilities.h"

#include <QRegExp>

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
#define pqCheckBoxSignal checkStateChanged
using pqCheckState = Qt::CheckState;
#else
#define pqCheckBoxSignal stateChanged
using pqCheckState = int;
#endif

static const int NO_COMPRESSION = 0;
static const int LZ4_COMPRESSION = 1;
static const int SQUIRT_COMPRESSION = 2;
static const int ZLIB_COMPRESSION = 3;
static const int NVPIPE_COMPRESSION = 4;
//-----------------------------------------------------------------------------

class pqImageCompressorWidget::pqInternals
{
public:
  Ui::ImageCompressorWidget Ui;
};

//-----------------------------------------------------------------------------
pqImageCompressorWidget::pqImageCompressorWidget(
  vtkSMProxy* smproxy, vtkSMProperty* smproperty, QWidget* parentObject)
  : Superclass(smproxy, parentObject)
  , Internals(new pqImageCompressorWidget::pqInternals())
{
  this->setShowLabel(false);

  Ui::ImageCompressorWidget& ui = this->Internals->Ui;
  ui.setupUi(this);
  pqWidgetUtilities::formatChildTooltips(this);
  ui.mainLayout->setContentsMargins(0, 0, 0, 0);
  ui.mainLayout->setSpacing(pqPropertiesPanel::suggestedVerticalSpacing());

  QWidget* separator = pqProxyWidget::newGroupLabelWidget(tr("Image Compression"), this);
  ui.mainLayout->insertWidget(0, separator, 1);

  this->connect(
    ui.compressionType, SIGNAL(currentIndexChanged(int)), SLOT(currentIndexChanged(int)));
  this->connect(
    ui.compressorBWOpt, SIGNAL(currentIndexChanged(int)), SLOT(setConfigurationDefault(int)));

  this->connect(
    ui.compressionType, SIGNAL(currentIndexChanged(int)), SIGNAL(compressorConfigChanged()));
  this->connect(ui.squirtColorSpace, SIGNAL(valueChanged(int)), SIGNAL(compressorConfigChanged()));
  this->connect(ui.zlibColorSpace, SIGNAL(valueChanged(int)), SIGNAL(compressorConfigChanged()));
  this->connect(ui.zlibLevel, SIGNAL(valueChanged(int)), SIGNAL(compressorConfigChanged()));
  QObject::connect(ui.zlibStripAlpha, &QCheckBox::pqCheckBoxSignal, this,
    &pqImageCompressorWidget::compressorConfigChanged);

#if VTK_MODULE_ENABLE_ParaView_nvpipe
  ui.compressionType->addItem("NvPipe");
  this->connect(ui.nvpLevel, SIGNAL(valueChanged(int)), SIGNAL(compressorConfigChanged()));
#endif

  this->addPropertyLink(this, "compressorConfig", SIGNAL(compressorConfigChanged()), smproperty);
}

//-----------------------------------------------------------------------------
pqImageCompressorWidget::~pqImageCompressorWidget()
{
  delete this->Internals;
  this->Internals = nullptr;
}

//-----------------------------------------------------------------------------
void pqImageCompressorWidget::setCompressorConfig(const QString& value)
{
  // FIXME: the format is wacky. The color space can only go from 0-5!!!
  // Need to fix it.
  Ui::ImageCompressorWidget& ui = this->Internals->Ui;
  QRegExp squirtRegExp("^vtkSquirtCompressor"
                       "\\s+"     // space
                       "0"        // 0
                       "\\s+"     // space
                       "([0-9]+)" // num-of-bits.
                       "$");
  QRegExp zlibRegExp("^vtkZlibImageCompressor"
                     "\\s+"
                     "0"
                     "\\s+"
                     "([0-9])" // compression level
                     "\\s+"
                     "([0-9]+)" // num-of-bits
                     "\\s+"
                     "([01])" // strip alpha (0 or 1).
                     "$");
  QRegExp lz4RegExp("^vtkLZ4Compressor"
                    "\\s+"     // space
                    "0"        // 0
                    "\\s+"     // space
                    "([0-9]+)" // num-of-bits.
                    "$");
  QRegExp nvpipeRegExp("^vtkNvPipeCompressor"
                       "\\s+"     // space
                       "0"        // 0
                       "\\s+"     // space
                       "([0-9]+)" // compression level.
                       "$");

  if (lz4RegExp.exactMatch(value))
  {
    int numBits = lz4RegExp.cap(1).toInt();
    ui.compressionType->setCurrentIndex(LZ4_COMPRESSION);
    ui.squirtColorSpace->setValue(numBits);
  }
  else if (squirtRegExp.exactMatch(value))
  {
    int numBits = squirtRegExp.cap(1).toInt();
    ui.compressionType->setCurrentIndex(SQUIRT_COMPRESSION);
    ui.squirtColorSpace->setValue(numBits);
  }
  else if (zlibRegExp.exactMatch(value))
  {
    int level = zlibRegExp.cap(1).toInt();
    int numBits = zlibRegExp.cap(2).toInt();
    bool stripAlpha = (zlibRegExp.cap(3).toInt() == 1);
    ui.compressionType->setCurrentIndex(ZLIB_COMPRESSION);
    ui.zlibLevel->setValue(level);
    ui.zlibColorSpace->setValue(numBits);
    ui.zlibStripAlpha->setCheckState(stripAlpha ? Qt::Checked : Qt::Unchecked);
  }
  else if (nvpipeRegExp.exactMatch(value))
  {
    int level = nvpipeRegExp.cap(1).toInt();
    ui.compressionType->setCurrentIndex(NVPIPE_COMPRESSION);
    ui.nvpLevel->setValue(level);
  }
  else
  {
    ui.compressionType->setCurrentIndex(NO_COMPRESSION);
  }
}

//-----------------------------------------------------------------------------
QString pqImageCompressorWidget::compressorConfig() const
{
  Ui::ImageCompressorWidget& ui = this->Internals->Ui;
  switch (ui.compressionType->currentIndex())
  {
    case LZ4_COMPRESSION:
      return QString("vtkLZ4Compressor 0 %1").arg(ui.squirtColorSpace->value());

    case SQUIRT_COMPRESSION: // squirt
      return QString("vtkSquirtCompressor 0 %1").arg(ui.squirtColorSpace->value());

    case ZLIB_COMPRESSION: // zlib
      return QString("vtkZlibImageCompressor 0 %1 %2 %3")
        .arg(ui.zlibLevel->value())
        .arg(ui.zlibColorSpace->value())
        .arg(ui.zlibStripAlpha->isChecked() ? 1 : 0);

    case NVPIPE_COMPRESSION: // nvpipe
      return QString("vtkNvPipeCompressor 0 %1").arg(ui.nvpLevel->value());
  }

  return QString("");
}

//-----------------------------------------------------------------------------
void pqImageCompressorWidget::currentIndexChanged(int index)
{
  Ui::ImageCompressorWidget& ui = this->Internals->Ui;
  ui.squirtLabel->setVisible(index == SQUIRT_COMPRESSION || index == LZ4_COMPRESSION);
  ui.squirtColorSpace->setVisible(index == SQUIRT_COMPRESSION || index == LZ4_COMPRESSION);

  ui.zlibLabel1->setVisible(index == ZLIB_COMPRESSION);
  ui.zlibLabel2->setVisible(index == ZLIB_COMPRESSION);
  ui.zlibLevel->setVisible(index == ZLIB_COMPRESSION);
  ui.zlibColorSpace->setVisible(index == ZLIB_COMPRESSION);
  ui.zlibStripAlpha->setVisible(index == ZLIB_COMPRESSION);

#if VTK_MODULE_ENABLE_ParaView_nvpipe
  ui.nvpLabel->setVisible(index == NVPIPE_COMPRESSION);
  ui.nvpLevel->setVisible(index == NVPIPE_COMPRESSION);
#else
  ui.nvpLabel->setVisible(false);
  ui.nvpLevel->setVisible(false);
#endif
}

//-----------------------------------------------------------------------------
void pqImageCompressorWidget::setConfigurationDefault(int index)
{
  enum
  {
    CONSUMER_DSL = 1,
    ETHERNET_1_MEG,
    ETHERNET_1_GIG,
    ETHERNET_10_GIG,
    SHARED_MEMORY
  };

  switch (index)
  {
    case CONSUMER_DSL:
      this->setCompressorConfig("vtkZlibImageCompressor 0 9 3 1");
      break;

    case ETHERNET_1_MEG:
      this->setCompressorConfig("vtkZlibImageCompressor 0 6 2 0");
      break;

    case ETHERNET_1_GIG:
      this->setCompressorConfig("vtkLZ4Compressor 0 5");
      break;

    case ETHERNET_10_GIG:
      this->setCompressorConfig("vtkLZ4Compressor 0 3");
      break;

    case SHARED_MEMORY:
    default:
      this->setCompressorConfig("");
  }
}
