// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkSMChartRepresentationProxy.h"

#include "vtkChartRepresentation.h"
#include "vtkClientServerStream.h"
#include "vtkObjectFactory.h"
#include "vtkPVExtractSelection.h"
#include "vtkPVXMLElement.h"
#include "vtkSMDomain.h"
#include "vtkSMProperty.h"
#include "vtkSMPropertyHelper.h"

vtkStandardNewMacro(vtkSMChartRepresentationProxy);
//----------------------------------------------------------------------------
vtkSMChartRepresentationProxy::vtkSMChartRepresentationProxy() = default;

//----------------------------------------------------------------------------
vtkSMChartRepresentationProxy::~vtkSMChartRepresentationProxy() = default;

//----------------------------------------------------------------------------
vtkChartRepresentation* vtkSMChartRepresentationProxy::GetRepresentation()
{
  this->CreateVTKObjects();
  return vtkChartRepresentation::SafeDownCast(this->GetClientSideObject());
}

//----------------------------------------------------------------------------
void vtkSMChartRepresentationProxy::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
int vtkSMChartRepresentationProxy::ReadXMLAttributes(
  vtkSMSessionProxyManager* pm, vtkPVXMLElement* element)
{
  if (!this->Superclass::ReadXMLAttributes(pm, element))
  {
    return 0;
  }

  vtkSMProxy* optionsProxy = this->GetSubProxy("PlotOptions");
  if (optionsProxy)
  {
    const char* names[] = { "Input", "Assembly", "BlockSelectors", "AttributeType", nullptr };
    for (int cc = 0; names[cc] != nullptr; cc++)
    {
      vtkSMProperty* src = this->GetProperty(names[cc]);
      vtkSMProperty* dest = optionsProxy->GetProperty(names[cc]);
      if (src && dest)
      {
        this->LinkProperty(src, dest);
      }
    }
  }
  return 1;
}

//----------------------------------------------------------------------------
void vtkSMChartRepresentationProxy::SetPropertyModifiedFlag(const char* name, int flag)
{
  if (name && strcmp(name, "Input") == 0)
  {
    vtkSMPropertyHelper helper(this, name);
    for (unsigned int cc = 0; cc < helper.GetNumberOfElements(); cc++)
    {
      vtkSMSourceProxy* input = vtkSMSourceProxy::SafeDownCast(helper.GetAsProxy(cc));
      if (input)
      {
        input->CreateSelectionProxies();
        vtkSMSourceProxy* esProxy = input->GetSelectionOutput(helper.GetOutputPort(cc));
        if (!esProxy)
        {
          vtkErrorMacro("Input proxy does not support selection extraction.");
        }
        else
        {
          vtkSMProxy* selectionReprProxy = this->GetSubProxy("SelectionRepresentation");
          if (selectionReprProxy)
          {
            // We use these internal properties since we need to add consumer dependency
            // on this proxy so that MarkModified() is called correctly.

            // Based on the name of the Property, we either pass the id-based
            // selection generated by vtkPVExtractSelection or the original
            // input selection to the selection representation.
            vtkSMPropertyHelper(selectionReprProxy, "SelectionInput", /*quiet*/ true)
              .Set(esProxy, vtkPVExtractSelection::OUTPUT_PORT_SELECTION_IDS);

            vtkSMPropertyHelper(selectionReprProxy, "OriginalSelectionInput", /*quiet*/ true)
              .Set(esProxy, vtkPVExtractSelection::OUTPUT_PORT_SELECTION_ORIGINAL);
            selectionReprProxy->UpdateVTKObjects();
          }
        }
      }
    }
  }
  this->Superclass::SetPropertyModifiedFlag(name, flag);
}
