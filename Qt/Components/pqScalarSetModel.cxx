// SPDX-FileCopyrightText: Copyright (c) Kitware Inc.
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause

#include "pqScalarSetModel.h"

#include <QList>

////////////////////////////////////////////////////////////////////////////
// pqScalarSetModel::pqImplementation

class pqScalarSetModel::pqImplementation
{
public:
  pqImplementation()
    : PreserveOrder(false)
    , Format('g')
    , Precision(9)
  {
  }

  QList<double> Values;
  bool PreserveOrder;
  char Format;
  int Precision;
};

/////////////////////////////////////////////////////////////////////////////
// pqScalarSetModel

pqScalarSetModel::pqScalarSetModel()
  : Implementation(new pqImplementation())
{
}

//-----------------------------------------------------------------------------
pqScalarSetModel::~pqScalarSetModel()
{
  delete this->Implementation;
}

//-----------------------------------------------------------------------------
void pqScalarSetModel::clear()
{
  this->Implementation->Values.clear();
  Q_EMIT layoutChanged();
}

//-----------------------------------------------------------------------------
QModelIndex pqScalarSetModel::insert(double value)
{
  QModelIndex mindex;
  if (this->Implementation->PreserveOrder)
  {
    this->Implementation->Values.append(value);
    mindex = this->createIndex(this->Implementation->Values.size() - 1, 0);
  }
  else
  {
    // if value is already contained, we don't add it.
    int curindex = this->Implementation->Values.indexOf(value);
    if (curindex == -1)
    {
      // insert at correct place.
      for (int cc = 0; cc < this->Implementation->Values.size(); cc++)
      {
        if (this->Implementation->Values[cc] > value)
        {
          curindex = cc;
          this->Implementation->Values.insert(curindex, value);
          break;
        }
      }
      if (curindex == -1)
      {
        curindex = this->Implementation->Values.size();
        this->Implementation->Values.append(value);
      }
    }
    mindex = this->createIndex(curindex, 0);
  }

  /*
  std::pair<std::set<double>::iterator,bool> iter=this->Implementation->Values.insert(value);
  int idx=std::distance(this->Implementation->Values.begin(),iter.first);
  QModelIndex mindex=this->createIndex(idx,0);
  */
  Q_EMIT layoutChanged();
  return mindex;
}

//-----------------------------------------------------------------------------
void pqScalarSetModel::erase(double value)
{
  this->Implementation->Values.removeAll(value);
  Q_EMIT layoutChanged();
}

//-----------------------------------------------------------------------------
void pqScalarSetModel::erase(int row)
{
  // NOLINTNEXTLINE(readability-redundant-casting): `qsizetype` in Qt6.
  if (row < 0 || row >= static_cast<int>(this->Implementation->Values.size()))
  {
    return;
  }

  this->Implementation->Values.removeAt(row);
  Q_EMIT layoutChanged();
}

//-----------------------------------------------------------------------------
QList<double> pqScalarSetModel::values()
{
  return this->Implementation->Values;
}

//-----------------------------------------------------------------------------
void pqScalarSetModel::setFormat(char f, int precision)
{
  this->Implementation->Format = f;
  this->Implementation->Precision = precision;
  Q_EMIT dataChanged(this->index(0), this->index(this->Implementation->Values.size() - 1));
}

//-----------------------------------------------------------------------------
QVariant pqScalarSetModel::data(const QModelIndex& i, int role) const
{
  if (!i.isValid())
    return QVariant();

  // NOLINTNEXTLINE(readability-redundant-casting): `qsizetype` in Qt6.
  if (i.row() < 0 || i.row() >= static_cast<int>(this->Implementation->Values.size()))
    return QVariant();

  switch (role)
  {
    case Qt::EditRole:
    case Qt::DisplayRole:
    {
      QList<double>::iterator iterator = this->Implementation->Values.begin();
      iterator += i.row();
      return QString::number(
        *iterator, this->Implementation->Format, this->Implementation->Precision);
    }
  }

  return QVariant();
}

//-----------------------------------------------------------------------------
Qt::ItemFlags pqScalarSetModel::flags(const QModelIndex& /*i*/) const
{
  return Qt::ItemIsEditable | Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

//-----------------------------------------------------------------------------
int pqScalarSetModel::rowCount(const QModelIndex& /*parent*/) const
{
  return Implementation->Values.size();
}

//-----------------------------------------------------------------------------
bool pqScalarSetModel::setData(const QModelIndex& i, const QVariant& value, int role)
{
  if (!i.isValid())
    return false;

  // NOLINTNEXTLINE(readability-redundant-casting): `qsizetype` in Qt6.
  if (i.row() < 0 || i.row() >= static_cast<int>(this->Implementation->Values.size()))
    return false;

  switch (role)
  {
    case Qt::EditRole:
    {
      /*
      std::set<double>::iterator iterator = this->Implementation->Values.begin();
      std::advance(iterator, i.row());
      this->Implementation->Values.erase(iterator);
      this->Implementation->Values.insert(value.toDouble());
      */
      this->erase(i.row());
      this->insert(value.toDouble());
      Q_EMIT dataChanged(this->index(0), this->index(this->Implementation->Values.size() - 1));
      Q_EMIT layoutChanged();
    }
  }

  return true;
}

//-----------------------------------------------------------------------------
void pqScalarSetModel::setPreserveOrder(bool preserve)
{
  this->Implementation->PreserveOrder = preserve;
}

//-----------------------------------------------------------------------------
bool pqScalarSetModel::preserveOrder() const
{
  return this->Implementation->PreserveOrder;
}
