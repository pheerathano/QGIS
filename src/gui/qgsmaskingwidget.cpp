/***************************************************************************
    qgsmaskingwidget.cpp
    ---------------------
    begin                : September 2019
    copyright            : (C) 2019 by Hugo Mercier
    email                : hugo dot mercier at oslandia dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QSet>

#include "qgsmaskingwidget.h"
#include "qgsmasksourceselectionwidget.h"
#include "qgssymbollayerselectionwidget.h"
#include "qgssymbollayerreference.h"
#include "qgsvectorlayer.h"
#include "qgssymbol.h"
#include "qgsstyleentityvisitor.h"
#include "symbology/qgsrenderer.h"
#include "qgsproject.h"
#include "qgsvectorlayerutils.h"
#include "qgsmasksymbollayer.h"
#include "qgsvectorlayerlabeling.h"

QgsMaskingWidget::QgsMaskingWidget( QWidget *parent ) :
  QgsPanelWidget( parent )
{
  setupUi( this );

  connect( mMaskTargetsWidget, &QgsSymbolLayerSelectionWidget::changed, this, [&]()
  {
    mMaskSourcesWidget->setEnabled( ! mMaskTargetsWidget->selection().isEmpty() );
    emit widgetChanged();
  } );
  connect( mMaskSourcesWidget, &QgsMaskSourceSelectionWidget::changed, this, [&]()
  {
    emit widgetChanged();
  } );
}

/**
 * \ingroup gui
 * Generic visitor that collects symbol layers of a vector layer's renderer
 * and call a callback function on them with their corresponding QgsSymbolLayerId
 *
 * \note This class is not a part of public API
 * \since QGIS 3.14
 */
class SymbolLayerVisitor : public QgsStyleEntityVisitorInterface
{
  public:
    typedef std::function<void( const QgsSymbolLayer *, const QgsSymbolLayerId & )> SymbolLayerCallback;

    //! constructor
    SymbolLayerVisitor( SymbolLayerCallback callback ) :
      mCallback( callback )
    {}

    bool visitEnter( const QgsStyleEntityVisitorInterface::Node &node ) override
    {
      if ( node.type != QgsStyleEntityVisitorInterface::NodeType::SymbolRule )
        return false;

      mSymbolKey = node.identifier;
      return true;
    }

    //! Process a symbol
    void visitSymbol( const QgsSymbol *symbol, const QString &leafIdentifier, QVector<int> rootPath )
    {
      for ( int idx = 0; idx < symbol->symbolLayerCount(); idx++ )
      {
        QVector<int> indexPath = rootPath;
        indexPath.push_back( idx );

        const QgsSymbolLayer *sl = symbol->symbolLayer( idx );

        mCallback( sl, QgsSymbolLayerId( mSymbolKey + leafIdentifier, indexPath ) );

        // recurse over sub symbols
        const QgsSymbol *subSymbol = const_cast<QgsSymbolLayer *>( sl )->subSymbol();
        if ( subSymbol )
          visitSymbol( subSymbol, leafIdentifier, indexPath );
      }
    }

    bool visit( const QgsStyleEntityVisitorInterface::StyleLeaf &leaf ) override
    {
      if ( leaf.entity && leaf.entity->type() == QgsStyle::SymbolEntity )
      {
        auto symbolEntity = static_cast<const QgsStyleSymbolEntity *>( leaf.entity );
        if ( symbolEntity->symbol() )
          visitSymbol( symbolEntity->symbol(), leaf.identifier, {} );
      }
      return true;
    }

  private:
    QString mSymbolKey;
    QList<QPair<QgsSymbolLayerId, QList<QgsSymbolLayerReference>>> mMasks;
    SymbolLayerCallback mCallback;
};

/**
 * Symbol layer masks collector. It is an enhanced version of QgsVectorLayerUtils::symbolLayerMasks.
 * Indeed, we need here to know both mask sources and targets for all masks
 *
 * Returns a list of pairs of:
 * - mask source symbol layer id
 * - list of target mask symbol layer references
 */
QList<QPair<QgsSymbolLayerId, QList<QgsSymbolLayerReference>>> symbolLayerMasks( const QgsVectorLayer *layer )
{
  if ( ! layer->renderer() )
    return {};

  QList<QPair<QgsSymbolLayerId, QList<QgsSymbolLayerReference>>> mMasks;
  SymbolLayerVisitor collector( [&]( const QgsSymbolLayer * sl, const QgsSymbolLayerId & lid )
  {
    if ( ! sl->masks().isEmpty() )
      mMasks.push_back( qMakePair( lid, sl->masks() ) );
  } );
  layer->renderer()->accept( &collector );
  return mMasks;
}

void QgsMaskingWidget::setLayer( QgsVectorLayer *layer )
{
  mLayer = layer;
  mMaskSourcesWidget->update();
  mMaskTargetsWidget->setLayer( layer );

  // collect masks and filter on those which have the current layer as destination
  QSet<QgsSymbolLayerId> maskedSymbolLayers;
  QList<QgsMaskSourceSelectionWidget::MaskSource> maskSources;
  QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();

  QgsMaskSourceSelectionWidget::MaskSource source;
  for ( auto layerIt = layers.begin(); layerIt != layers.end(); layerIt++ )
  {
    QString layerId = layerIt.key();
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer *>( layerIt.value() );
    if ( ! vl )
      continue;

    // collect symbol layer masks
    QList<QPair<QgsSymbolLayerId, QList<QgsSymbolLayerReference>>> slMasks = symbolLayerMasks( vl );
    for ( auto p : slMasks )
    {
      const QgsSymbolLayerId &sourceSymbolLayerId = p.first;
      for ( const QgsSymbolLayerReference &ref : p.second )
      {
        if ( ref.layerId() == mLayer->id() )
        {
          // add to the set of destinations
          maskedSymbolLayers.insert( ref.symbolLayerId() );
          // add to the list of mask sources
          source.layerId = layerId;
          source.isLabeling = false;
          source.symbolLayerId = sourceSymbolLayerId;
          maskSources.append( source );
        }
      }
    }

    // collect label masks
    QHash<QString, QHash<QString, QSet<QgsSymbolLayerId>>> labelMasks = QgsVectorLayerUtils::labelMasks( vl );
    for ( auto it = labelMasks.begin(); it != labelMasks.end(); it++ )
    {
      const QString &ruleKey = it.key();
      for ( auto it2 = it.value().begin(); it2 != it.value().end(); it2++ )
      {
        if ( it2.key() == mLayer->id() )
        {
          // merge with masked symbol layers
          maskedSymbolLayers.unite( it2.value() );
          // add the mask source
          source.layerId = layerId;
          source.isLabeling = true;
          source.symbolLayerId = QgsSymbolLayerId( ruleKey, {} );
          maskSources.append( source );
        }
      }
    }
  }

  mMaskSourcesWidget->setSelection( maskSources );
  mMaskTargetsWidget->setSelection( maskedSymbolLayers );
}

void QgsMaskingWidget::apply()
{
  QList<QgsMaskSourceSelectionWidget::MaskSource> maskSources = mMaskSourcesWidget->selection();
  QSet<QgsSymbolLayerId> maskedSymbolLayers = mMaskTargetsWidget->selection();

  QSet<QString> layersToRefresh;

  QMap<QString, QgsMapLayer *> layers = QgsProject::instance()->mapLayers();
  for ( auto layerIt = layers.begin(); layerIt != layers.end(); layerIt++ )
  {
    QgsVectorLayer *vl = qobject_cast<QgsVectorLayer *>( layerIt.value() );
    if ( ! vl )
      continue;

    //
    // First reset symbol layer masks
    SymbolLayerVisitor maskSetter( [&]( const QgsSymbolLayer * sl, const QgsSymbolLayerId & slId )
    {
      if ( sl->layerType() == "MaskMarker" )
      {
        QgsMaskMarkerSymbolLayer *maskSl = const_cast<QgsMaskMarkerSymbolLayer *>( static_cast<const QgsMaskMarkerSymbolLayer *>( sl ) );

        QgsSymbolLayerReferenceList masks = maskSl->masks();
        QgsSymbolLayerReferenceList newMasks;
        for ( const QgsSymbolLayerReference &ref : masks )
        {
          // copy the original masks, only those with another destination layer
          if ( ref.layerId() != mLayer->id() )
            newMasks.append( ref );
        }
        for ( const QgsMaskSourceSelectionWidget::MaskSource &source : maskSources )
        {
          if ( ! source.isLabeling && source.layerId == layerIt.key() && source.symbolLayerId == slId )
          {
            // ... then add the new masked symbol layers, if any
            for ( const QgsSymbolLayerId &maskedId : maskedSymbolLayers )
            {
              newMasks.append( QgsSymbolLayerReference( mLayer->id(), maskedId ) );
            }
            // invalidate the cache of the source layer
            layersToRefresh.insert( source.layerId );
          }
        }
        maskSl->setMasks( newMasks );
      }
    } );
    if ( vl->renderer() )
      vl->renderer()->accept( &maskSetter );

    //
    // Now reset label masks
    if ( ! vl->labeling() )
      continue;
    for ( QString labelProvider : vl->labeling()->subProviders() )
    {
      // clear symbol layers
      QgsPalLayerSettings settings = vl->labeling()->settings( labelProvider );
      QgsTextFormat format = settings.format();
      if ( ! format.mask().enabled() )
        continue;
      QgsSymbolLayerReferenceList masks = format.mask().maskedSymbolLayers();
      QgsSymbolLayerReferenceList newMasks;
      for ( const QgsSymbolLayerReference &ref : masks )
      {
        // copy the original masks, only those with another destination layer
        if ( ref.layerId() != mLayer->id() )
          newMasks.append( ref );
      }
      for ( const QgsMaskSourceSelectionWidget::MaskSource &source : maskSources )
      {
        // ... then add the new masked symbol layers, if any

        if ( source.isLabeling && source.layerId == layerIt.key() && source.symbolLayerId.symbolKey() == labelProvider )
        {
          for ( const QgsSymbolLayerId &maskedId : maskedSymbolLayers )
          {
            newMasks.append( QgsSymbolLayerReference( mLayer->id(), maskedId ) );
          }
          // invalidate the cache of the source layer
          layersToRefresh.insert( source.layerId );
        }
      }
      format.mask().setMaskedSymbolLayers( newMasks );
      settings.setFormat( format );
      vl->labeling()->setSettings( new QgsPalLayerSettings( settings ), labelProvider );
    }
  }

  QgsProject::instance()->setDirty();
  // trigger refresh of the current layer
  mLayer->triggerRepaint();
  // trigger refresh of dependent layers (i.e. mask source layers)
  for ( QString layerId : layersToRefresh )
  {
    QgsMapLayer *layer = QgsProject::instance()->mapLayer( layerId );
    layer->triggerRepaint();
  }
}
