/***************************************************************************
    qgsexpressionstoredialog.h
    ---------------------
    begin                : December 2019
    copyright            : (C) 2019 by Alessandro Pasotti
    email                : elpaso at itopen dot it
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSEXPRESSIONSTOREDIALOG_H
#define QGSEXPRESSIONSTOREDIALOG_H

#include "qgis_gui.h"
#include <QDialog>
#include "ui_qgsexpressionstoredialogbase.h"



/**
 * \ingroup gui
 * A generic dialog for editing expression text, label and help text.
 * \since QGIS 3.12
 */
class GUI_EXPORT QgsExpressionStoreDialog : public QDialog, private Ui::QgsExpressionStoreDialogBase
{
  public:

    /**
     * Creates a QgsExpressionStoreDialog with given \a label, \a expression and \a helpText.
     * \a existingLabels is an optional list of existing labels for unique label validation,
     * \a parent is the optional parent widget.
     */
    QgsExpressionStoreDialog( const QString &label,
                              const QString &expression,
                              const QString &helpText,
                              const QStringList &existingLabels = QStringList(),
                              QWidget *parent = nullptr );

    /**
     * Returns the expression text
     */
    QString expression( ) { return mExpression->text( ); }

    /**
     * Returns the label text
     */
    QString label() { return  mLabel->text(); }

    /**
     * Returns the help text
     */
    QString helpText() { return  mHelpText->toHtml(); }

  private:

    QStringList mExistingLabels;

};

#endif // QGSEXPRESSIONSTOREDIALOG_H
