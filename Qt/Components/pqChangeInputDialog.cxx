// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "pqChangeInputDialog.h"
#include "ui_pqChangeInputDialog.h"

#include "pqActiveObjects.h"
#include "pqApplicationCore.h"
#include "pqOutputPort.h"
#include "pqPipelineFilter.h"
#include "pqPipelineModel.h"
#include "pqPropertyWidget.h"
#include "pqServer.h"
#include "pqServerManagerModel.h"
#include "vtkSMDocumentation.h"
#include "vtkSMInputProperty.h"
#include "vtkSMPropertyHelper.h"
#include "vtkSMProxy.h"
#include "vtkSmartPointer.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QRadioButton>

#include <cassert>

class pqChangeInputDialog::pqInternals : public Ui::pqChangeInputDialog
{
public:
  pqPipelineModel* PipelineModel;
  vtkSmartPointer<vtkSMProxy> Proxy;

  QString ActiveInputProperty;
  bool BlockSelectionChanged;
  QMap<QString, QList<pqOutputPort*>> Inputs;
  QMap<QString, bool> AcceptsMultipleConnections;
};

//-----------------------------------------------------------------------------
pqChangeInputDialog::pqChangeInputDialog(vtkSMProxy* filterProxy, QWidget* parentObject)
  : Superclass(parentObject)
{
  assert(filterProxy != nullptr);

  this->Internals = new pqInternals();
  this->Internals->Proxy = filterProxy;
  this->Internals->BlockSelectionChanged = false;

  this->Internals->setupUi(this);
  // hide the Context Help item (it's a "?" in the Title Bar for Windows, a menu item for Linux)
  this->setWindowFlags(this->windowFlags().setFlag(Qt::WindowContextHelpButtonHint, false));

  pqServerManagerModel* smModel = pqApplicationCore::instance()->getServerManagerModel();
  this->Internals->PipelineModel = new pqPipelineModel(*smModel, this);
  this->Internals->PipelineModel->setEditable(false);
  this->Internals->pipelineView->setModel(this->Internals->PipelineModel);

  this->Internals->pipelineView->getHeader()->hide();
  // don't show the visibility icons.
  this->Internals->pipelineView->getHeader()->hideSection(1);
  this->Internals->pipelineView->setRootIndex(
    this->Internals->PipelineModel->getIndexFor(pqActiveObjects::instance().activeServer()));
  this->Internals->pipelineView->expandAll();

  QObject::connect(this->Internals->pipelineView->getSelectionModel(),
    SIGNAL(selectionChanged(const QItemSelection&, const QItemSelection&)), this,
    SLOT(selectionChanged()));

  // build current input list.
  QList<const char*> input_property_names = pqPipelineFilter::getInputPorts(filterProxy);
  Q_FOREACH (const char* property_name, input_property_names)
  {
    QList<pqOutputPort*>& list = this->Internals->Inputs[property_name];

    vtkSMPropertyHelper helper(filterProxy, property_name);
    unsigned int numProxies = helper.GetNumberOfElements();
    for (unsigned int cc = 0; cc < numProxies; cc++)
    {
      pqPipelineSource* source = smModel->findItem<pqPipelineSource*>(helper.GetAsProxy(cc));
      if (source == nullptr)
      {
        qDebug("Not all current inputs to this filter are know to the ProxyManager");
        continue;
      }
      pqOutputPort* port = source->getOutputPort(helper.GetOutputPort(cc));
      list.push_back(port);
    }

    // check if multiple connections are accepted by this input property.
    vtkSMInputProperty* ip =
      vtkSMInputProperty::SafeDownCast(filterProxy->GetProperty(property_name));
    if (ip && ip->GetMultipleInput())
    {
      this->Internals->AcceptsMultipleConnections[property_name] = true;
    }
    else
    {
      this->Internals->AcceptsMultipleConnections[property_name] = false;
    }
  }

  this->buildPortWidgets();
}

//-----------------------------------------------------------------------------
pqChangeInputDialog::~pqChangeInputDialog()
{
  delete this->Internals;
  this->Internals = nullptr;
}

//-----------------------------------------------------------------------------
const QMap<QString, QList<pqOutputPort*>>& pqChangeInputDialog::selectedInputs() const
{
  return this->Internals->Inputs;
}

//-----------------------------------------------------------------------------
void pqChangeInputDialog::buildPortWidgets()
{
  QVBoxLayout* vbox = qobject_cast<QVBoxLayout*>(this->Internals->buttonFrame->layout());

  // we use this since it returns a nice ordered list of the input port names
  // rather than an alphabetically sorted one.
  QList<const char*> input_property_names = pqPipelineFilter::getInputPorts(this->Internals->Proxy);
  QRadioButton* firstButton = nullptr;
  for (int cc = 0; cc < input_property_names.size(); cc++)
  {
    const char* input_prop_name = input_property_names[cc];
    vtkSMProperty* smproperty = this->Internals->Proxy->GetProperty(input_prop_name);
    QRadioButton* rb = new QRadioButton(this->Internals->buttonFrame);
    rb->setObjectName(input_prop_name);
    rb->setText(QCoreApplication::translate("ServerManagerXML", smproperty->GetXMLLabel()));
    if (smproperty->GetDocumentation())
    {
      rb->setToolTip(pqPropertyWidget::getTooltip(smproperty));
    }
    vbox->addWidget(rb);

    QObject::connect(rb, SIGNAL(toggled(bool)), this, SLOT(inputPortToggled(bool)));

    firstButton = firstButton ? firstButton : rb;
  }
  vbox->addStretch();

  if (firstButton)
  {
    firstButton->setChecked(true);
  }

  if (this->Internals->Inputs.size() <= 1)
  {
    // no need to show the radio buttons when there's only 1 input port.
    this->Internals->buttonFrame->hide();
    this->Internals->line->hide();
    this->layout()->removeWidget(this->Internals->buttonFrame);
    this->layout()->removeWidget(this->Internals->line);
  }
}

//-----------------------------------------------------------------------------
void pqChangeInputDialog::inputPortToggled(bool checked)
{
  if (!checked)
  {
    return;
  }

  QRadioButton* radioButton = qobject_cast<QRadioButton*>(this->sender());
  this->Internals->selectInputLabel->setText(tr("Select <b>%1</b>").arg(radioButton->text()));

  QModelIndexList selected_indexes;
  QString input_prop_name = radioButton->objectName();
  this->Internals->ActiveInputProperty = input_prop_name;

  QList<pqOutputPort*>& list = this->Internals->Inputs[input_prop_name];
  Q_FOREACH (pqOutputPort* port, list)
  {
    selected_indexes.push_back(this->Internals->PipelineModel->getIndexFor(port));
  }

  // update selectability i.e. what sources in the pipelines are acceptable as
  // inputs for the current input port.
  pqServerManagerModel* smModel = pqApplicationCore::instance()->getServerManagerModel();
  this->Internals->PipelineModel->setSubtreeSelectable(
    pqActiveObjects::instance().activeServer(), true);
  pqPipelineSource* pqproxy = smModel->findItem<pqPipelineSource*>(this->Internals->Proxy);
  if (pqproxy)
  {
    this->Internals->PipelineModel->setSubtreeSelectable(pqproxy, false);
  }

  vtkSMInputProperty* inputProp = vtkSMInputProperty::SafeDownCast(
    this->Internals->Proxy->GetProperty(input_prop_name.toUtf8().data()));

  QModelIndex root = this->Internals->pipelineView->getRootIndex();
  QModelIndex index = this->Internals->PipelineModel->getNextIndex(root, root);
  while (index.isValid())
  {
    if (this->Internals->PipelineModel->isSelectable(index))
    {
      pqServerManagerModelItem* item = this->Internals->PipelineModel->getItemFor(index);
      pqPipelineSource* source = qobject_cast<pqPipelineSource*>(item);
      pqOutputPort* port = qobject_cast<pqOutputPort*>(item);
      if (source)
      {
        port = source->getOutputPort(0);
      }
      if (source && source->getNumberOfOutputPorts() > 1)
      {
        this->Internals->PipelineModel->setSelectable(index, false);
      }
      else if (port)
      {
        inputProp->RemoveAllUncheckedProxies();
        inputProp->AddUncheckedInputConnection(
          port->getSource()->getProxy(), port->getPortNumber());
        this->Internals->PipelineModel->setSelectable(index, inputProp->IsInDomains() > 0);
        inputProp->RemoveAllUncheckedProxies();
      }
      else
      {
        this->Internals->PipelineModel->setSelectable(index, false);
      }
    }
    index = this->Internals->PipelineModel->getNextIndex(index, root);
  }

  // update selection mode.
  if (this->Internals->AcceptsMultipleConnections[input_prop_name])
  {
    this->Internals->pipelineView->setSelectionMode(pqFlatTreeView::ExtendedSelection);
  }
  else
  {
    this->Internals->pipelineView->setSelectionMode(pqFlatTreeView::SingleSelection);
  }

  // update current selection.
  this->Internals->BlockSelectionChanged = true;
  QItemSelectionModel* selModel = this->Internals->pipelineView->getSelectionModel();
  selModel->clear();
  Q_FOREACH (QModelIndex idx, selected_indexes)
  {
    selModel->select(idx, QItemSelectionModel::Select);
  }
  this->Internals->BlockSelectionChanged = false;
  this->selectionChanged();
}

//-----------------------------------------------------------------------------
void pqChangeInputDialog::selectionChanged()
{
  if (this->Internals->BlockSelectionChanged)
  {
    return;
  }

  QList<pqOutputPort*>& list = this->Internals->Inputs[this->Internals->ActiveInputProperty];
  list.clear();

  QItemSelectionModel* selModel = this->Internals->pipelineView->getSelectionModel();
  QModelIndexList indexes = selModel->selectedIndexes();
  Q_FOREACH (QModelIndex idx, indexes)
  {
    pqServerManagerModelItem* item = this->Internals->PipelineModel->getItemFor(idx);
    pqOutputPort* port = qobject_cast<pqOutputPort*>(item);
    pqPipelineSource* source = qobject_cast<pqPipelineSource*>(item);
    if (source)
    {
      port = source->getOutputPort(0);
    }
    list.push_back(port);
  }
}
