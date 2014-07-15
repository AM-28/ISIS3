#include "IsisDebug.h"

#include "MatrixSceneWidget.h"

#include <float.h>
#include <sstream>

#include <QtCore>
#include <QtGui>
#include <QtXml>
#include <QList>
#include <QPen>
#include <QMap>
#include <QGraphicsRectItem>

#include "CorrelationMatrix.h"
#include "Directory.h"
#include "Distance.h"
#include "FileName.h"
#include "GraphicsView.h"
#include "IException.h"
#include "IString.h"
#include "MatrixGraphicsScene.h"
#include "MatrixGraphicsView.h"
// #include "MatrixOptions.h"
#include "ProgressBar.h"
#include "Project.h"
#include "PvlObject.h"
#include "Pvl.h"
#include "SparseBlockMatrix.h"
#include "TextFile.h"
#include "Target.h"
#include "ToolPad.h"
#include "XmlStackedHandlerReader.h"

#include <boost/numeric/ublas/fwd.hpp>
#include <boost/numeric/ublas/matrix_sparse.hpp>
#include <boost/numeric/ublas/matrix_proxy.hpp>
#include <boost/numeric/ublas/io.hpp>

using namespace boost::numeric::ublas;
namespace Isis {
  /**
   * Create a scene widget.
   *
   * @param status
   * @param showTools
   * @param internalizeToolBarsAndProgress
   * @param directory
   * @param parent
   */
  MatrixSceneWidget::MatrixSceneWidget(QStatusBar *status,
                                       bool showTools,
                                       bool internalizeToolBarsAndProgress,
                                       Directory *directory,
                                       QWidget *parent) : QWidget(parent) {
    m_directory = directory;

    m_graphicsScene = new MatrixGraphicsScene(this);
    m_graphicsScene->installEventFilter(this);

    m_graphicsView = new MatrixGraphicsView(m_graphicsScene, this);
    m_graphicsView->setScene(m_graphicsScene);
    m_graphicsView->setInteractive(true);
    
    // Draw Initial Matrix
    drawElements( m_directory->project()->correlationMatrix() );
    drawGrid( m_directory->project()->correlationMatrix() );

//     m_matrixOptions = new MatrixOptions(m_directory->project()->correlationMatrix(), this);
    
    m_graphicsView->setResizeAnchor(QGraphicsView::AnchorViewCenter);

    m_quickMapAction = NULL;

    m_outlineRect = NULL;

    m_progress = new ProgressBar;
    m_progress->setVisible(false);

    QGridLayout * sceneLayout = new QGridLayout;
    sceneLayout->setContentsMargins(0, 0, 0, 0);
    setLayout(sceneLayout);

    // If we are making our own layout, we can create our own status area.
    if (!status && internalizeToolBarsAndProgress)
      status = new QStatusBar;

    if (showTools) {

      if (internalizeToolBarsAndProgress) {
        
        sceneLayout->addWidget(m_graphicsView, 1, 1, 1, 1);
        
        QHBoxLayout *horizontalStatusLayout = new QHBoxLayout;
        horizontalStatusLayout->addWidget(m_progress);
        horizontalStatusLayout->addStretch();
        horizontalStatusLayout->addWidget(status);

        sceneLayout->addLayout(horizontalStatusLayout, 2, 0, 1, 2);
      }
      else {
        sceneLayout->addWidget(m_graphicsView, 0, 0, 1, 1);
      }

      setWhatsThis("This is the matrix scene. You can interact with the "
                   "matrix elements shown here. ");

      getView()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
      getView()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

      getView()->enableResizeZooming(false);

      connect( getView()->horizontalScrollBar(), SIGNAL( valueChanged(int) ),
               this, SLOT( sendVisibleRectChanged() ) );
      connect( getView()->verticalScrollBar() , SIGNAL( valueChanged(int) ),
               this, SLOT( sendVisibleRectChanged() ) );
      connect( getView()->horizontalScrollBar(), SIGNAL( rangeChanged(int, int) ),
               this, SLOT( sendVisibleRectChanged() ) );
      connect( getView()->verticalScrollBar() , SIGNAL( rangeChanged(int, int) ),
               this, SLOT( sendVisibleRectChanged() ) );
    }
    else {
      sceneLayout->addWidget(m_graphicsView, 0, 0, 1, 1);

      getView()->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      getView()->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

      setWhatsThis("This is the matrix world view. The matrix will be "
          "shown here, but you cannot zoom in.");
    }
//     draw elements
  }



  /**
   * Default Destructor
   */
  MatrixSceneWidget::~MatrixSceneWidget() {
    m_outlineRect = NULL; // The scene will clean this up
  }


  /**
   * Accessor for the QGraphicsView
   *
   * @return MatrixGraphicsView The matrix view.
   */
  MatrixGraphicsView *MatrixSceneWidget::getView() const {
    return m_graphicsView;
  }


  
  /**
   * Accessor for the QGraphicsScene
   *
   * @return MatrixGraphicsScene The matrix scene.
   */
  QGraphicsScene *MatrixSceneWidget::getScene() const {
    return m_graphicsScene;
  }



  /**
   * This is called by MatrixGraphicsScene::contextMenuEvent.
   *
   * Return false if not handled, true if handled.
   */
  bool MatrixSceneWidget::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    bool handled = false;

    QList<QGraphicsItem *> selectedGraphicsItems = getScene()->selectedItems();
    // qDebug() << "number of selected items:" << selectedGraphicsItems.count();
//     QList<MatrixSceneItem *> selectedMatrixElementItems;
//     QList<MatrixElement*> selectedMatrixElements;
// 
//     foreach (QGraphicsItem *graphicsItem, selectedGraphicsItems) {
//       MatrixSceneItem *sceneMatrixElementItem = dynamic_cast<MatrixSceneItem *>(graphicsItem);
// 
//       if (!sceneMatrixElementItem) {
//         sceneMatrixElementItem = dynamic_cast<MatrixSceneItem *>( graphicsItem->parentItem() );
//       }
// 
//       if ( sceneMatrixElementItem && sceneMatrixElementItem->element() ) {
//         selectedMatrixElementItems.append(sceneMatrixElementItem);
//         selectedMatrixElements.append( sceneMatrixElementItem->element() );
//       }
//     }
// 
//     if ( selectedMatrixElementItems.count() ) {
//       QMenu menu;
// 
//       QAction *title = menu.addAction( tr("%L1 Selected MatrixElements").arg( selectedMatrixElements.count() ) );
//       title->setEnabled(false);
//       menu.addSeparator();
// 
//       Project *project = m_directory ? m_directory->project() : NULL;
// 
//       QList<QAction *> displayActs = selectedMatrixElements.supportedActions(project);
// 
//       if (m_directory) {
//         displayActs.append(NULL);
//         displayActs.append( m_directory->supportedActions( new QList<MatrixElement*>(selectedMatrixElements) ) );
//       }
// 
//       QAction *displayAct;
//       foreach(displayAct, displayActs) {
//         if (displayAct == NULL) {
//           menu.addSeparator();
//         }
//         else {
//           menu.addAction(displayAct);
//         }
//       }
// 
//       handled = true;
//       menu.exec( event->screenPos() );
//     }

    return handled;
  }



  
  QProgressBar *MatrixSceneWidget::getProgress() {
    return m_progress;
  }



  void MatrixSceneWidget::load(XmlStackedHandlerReader *xmlReader) {
    xmlReader->pushContentHandler( new XmlHandler(this) );
  }


  QRectF MatrixSceneWidget::elementsBoundingRect() const {
    QRectF boundingRect;

    if (m_outlineRect)
      boundingRect = boundingRect.united( m_outlineRect->boundingRect() );

    return boundingRect;
  }



  Directory *MatrixSceneWidget::directory() const {
    return m_directory;
  }



  QList<QAction *> MatrixSceneWidget::getViewActions() {
    QList<QAction *> viewActs;

//     foreach(MatrixTool *tool, *m_tools) {
//       QList<QAction *> toolViewActs = tool->getViewActions();
//       viewActs.append(toolViewActs);
//     }

    return viewActs;
  }


  
  /**
   * Get a list of applicable actions for the elements in this scene.
   *
   * @param
   * @return
   */
  QList<QAction *> MatrixSceneWidget::supportedActions(CorrelationMatrix *matrix) {
    QList<QAction *> results;

//     bool allMatrixElementsInView = !matrix->visibleElements().isEmpty();

    // Add visible elements to scene
    
//     foreach (MatrixElement *element, *elements) {
//       allMatrixElementsInView = allMatrixElementsInView && (elementToMatrix(element) != NULL);
//     }

    // Add actions to return list
//     if (allMatrixElementsInView) {
//       MoveToTopSceneWorkOrder *moveToTopAct =
//           new MoveToTopSceneWorkOrder( this, m_directory->project() );
//       moveToTopAct->setData(elements);
//       results.append(moveToTopAct);
// 
//       MoveUpOneSceneWorkOrder *moveUpOneAct =
//           new MoveUpOneSceneWorkOrder( this, m_directory->project() );
//       moveUpOneAct->setData(elements);
//       results.append(moveUpOneAct);
// 
//       MoveToBottomSceneWorkOrder *moveToBottomAct =
//           new MoveToBottomSceneWorkOrder( this, m_directory->project() );
//       moveToBottomAct->setData(elements);
//       results.append(moveToBottomAct);
// 
//       MoveDownOneSceneWorkOrder *moveDownOneAct =
//           new MoveDownOneSceneWorkOrder( this, m_directory->project() );
//       moveDownOneAct->setData(elements);
//       results.append(moveDownOneAct);

      // Add separator to contect menu
//       results.append(NULL);
/*
      QAction *zoomFitAct = new QAction(tr("Zoom Fit"), this);
      zoomFitAct->setData( qVariantFromValue( matrix->visibleElements() ) );
      connect( zoomFitAct, SIGNAL( triggered() ),
               this, SLOT( fitInView() ) );
      results.append(zoomFitAct);*/
//     }

    return results;
  }


  
  /**
   * Saves the scene as a png, jpg, or tif file.
   */
  void MatrixSceneWidget::exportView() {
    QString output =
      QFileDialog::getSaveFileName( (QWidget *)parent(),
                                    "Choose output file",
                                    QDir::currentPath() + "/untitled.png",
                                    QString("MatrixElements (*.png *.jpg *.tif)") );
    if ( output.isEmpty() ) return;

    // Use png format is the user did not add a suffix to their output filename.
    if ( QFileInfo(output).suffix().isEmpty() ) {
      output = output + ".png";
    }

    QString format = QFileInfo(output).suffix();
    QPixmap pm = QPixmap::grabWidget( getScene()->views().last() );

    std::string formatString = format.toStdString();
    if ( !pm.save( output, formatString.c_str() ) ) {
      QMessageBox::information(this, "Error",
                               "Unable to save [" + output + "]");
    }
  }


  void MatrixSceneWidget::saveList() {
    QString output = QFileDialog::getSaveFileName( (QWidget *)parent(),
                                                   "Choose output file",
                                                   QDir::currentPath() + "/files.lis",
                                                   QString("List File (*.lis);;Text File (*.txt);;"
                                                           "All Files (*.*)") );
    if ( output.isEmpty() ) return;

    TextFile file(output, "overwrite");
  }



  /**
   * This method refits the items in the graphics view.
   *
   */
//   void MatrixSceneWidget::refit() {
//     QRectF sceneRect = elementsBoundingRect();
// 
//     if ( sceneRect.isEmpty() )
//       return;
// 
//     double xPadding = sceneRect.width() * 0.10;
//     double yPadding = sceneRect.height() * 0.10;
// 
//     sceneRect.adjust(-xPadding, -yPadding, xPadding, yPadding);
//     getView()->fitInView(sceneRect, Qt::KeepAspectRatio);
//   }



  void MatrixSceneWidget::sendVisibleRectChanged() {
    QPointF topLeft = getView()->mapToScene(0, 0);
    QPointF bottomRight =
                getView()->mapToScene( (int)getView()->width(), (int)getView()->height() );

    QRectF visibleRect(topLeft, bottomRight);
    emit visibleRectChanged(visibleRect);
  }



  /**
   * Pick the relevant actions given the type of event that occured in the scene.
   *
   * Mouse Events:
   * 
   * Press - Add matrix value to list?
   * Release - nolowerColorValue?
   * Double-Click - same as press
   * Move - nolowerColorValue
   * Enter - change label to match current item.
   * Leave - clear label
   * 
   * @param obj
   * @param event
   *
   * @return bool 
   */
  bool MatrixSceneWidget::eventFilter(QObject *obj, QEvent *event) {
    bool stopProcessingEvent = true;

    switch( event->type() ) {
      case QMouseEvent::GraphicsSceneMousePress: {

        emit mouseButtonPress( ( (QGraphicsSceneMouseEvent *)event )->scenePos(),
                               ( (QGraphicsSceneMouseEvent *)event )->button() );

        // do stuff with getScene()->itemAt(screenPos)
        m_graphicsScene->itemAt( ( (QGraphicsSceneMouseEvent *)event )->scenePos() )->setSelected(true);
//         rect->setColor(Qt::red);

        

        stopProcessingEvent = false;
        break;
      }

      case QMouseEvent::GraphicsSceneMouseRelease: {
        bool signalEmitted = false;

        if (!signalEmitted) {
          stopProcessingEvent = false;
          emit mouseButtonRelease( ( (QGraphicsSceneMouseEvent *)event )->scenePos(),
                                   ( (QGraphicsSceneMouseEvent *)event )->button() );
        }
        break;
      }

      case QMouseEvent::GraphicsSceneMouseDoubleClick:
        // Do the same lowerColorValue as single click
        emit mouseDoubleClick( ( (QGraphicsSceneMouseEvent *)event )->scenePos() );
        stopProcessingEvent = false;
        break;

      case QMouseEvent::GraphicsSceneMouseMove:
        // this will chagne the values in the options dialog.
//         QPointF scenePos = ( (QGraphicsSceneMouseEvent *)event )->scenePos();
//         QPoint screenPos = getView()->mapFromScene(scenePos);

        // do stuff with getScene()->itemAt(screenPos)
        
        stopProcessingEvent = false;
        

        emit mouseMove( ( (QGraphicsSceneMouseEvent *)event )->scenePos() );
        break;

      case QEvent::GraphicsSceneWheel:
        emit mouseWheel( ( (QGraphicsSceneWheelEvent *)event )->scenePos(),
                         ( (QGraphicsSceneWheelEvent *)event )->delta() );
        event->accept();
        stopProcessingEvent = true;
        break;

      case QMouseEvent::Enter:
        emit mouseEnter();
        stopProcessingEvent = false;
        break;

      case QMouseEvent::Leave:
        emit mouseLeave();
        stopProcessingEvent = false;
        break;

      case QEvent::GraphicsSceneHelp: {
        setToolTip("");
        bool toolTipFound = false;

        QGraphicsItem *sceneItem;
        foreach(sceneItem, getScene()->items() ) {
          if (!toolTipFound) {
            if (sceneItem->contains( ( (QGraphicsSceneHelpEvent*)event )->scenePos() ) &&
                sceneItem->toolTip().size() > 0) {
              setToolTip( sceneItem->toolTip() );
              toolTipFound = true;
            }
          }
        }

        if (toolTipFound) {
          stopProcessingEvent = true;
          QToolTip::showText( ( (QGraphicsSceneHelpEvent*)event )->screenPos(), toolTip() );
        }
        break;
      }

      default:
        stopProcessingEvent = false;
        break;
    }

    return stopProcessingEvent;
  }

  

  /**
   * Redraws all the items in the view. Also makes sure to resize the view rectangle
   * to fit the newly drawn items.
   */
  void MatrixSceneWidget::redrawItems() {
  }


  /**
   * Refit scene and items when the widget size changes.
   */
  void MatrixSceneWidget::fitInView() {
  }




/**********************************************************************************************
 * Start XmlHandler Methods
 **********************************************************************************************/


  MatrixSceneWidget::XmlHandler::XmlHandler(MatrixSceneWidget *scene) {
    m_sceneWidget = scene;
    m_scrollBarXValue = -1;
    m_scrollBarYValue = -1;
  }


  MatrixSceneWidget::XmlHandler::~XmlHandler() {
  }


  bool MatrixSceneWidget::XmlHandler::startElement(const QString &namespaceURI,
      const QString &localName, const QString &qName, const QXmlAttributes &atts) {
    bool result = XmlStackedHandler::startElement(namespaceURI, localName, qName, atts);

    return result;
  }


  bool MatrixSceneWidget::XmlHandler::characters(const QString &ch) {
    bool result = XmlStackedHandler::characters(ch);

    if (result) {
      m_characterData += ch;
    }

    return result;
  }


  bool MatrixSceneWidget::XmlHandler::endElement(const QString &namespaceURI,
      const QString &localName, const QString &qName) {
    bool result = XmlStackedHandler::endElement(namespaceURI, localName, qName);

    if (result) {
      if (localName == "viewTransform") {
        QByteArray hexValues( m_characterData.toAscii() );
        QDataStream transformStream( QByteArray::fromHex(hexValues) );

        QTransform viewTransform;
        transformStream >> viewTransform;
        m_sceneWidget->getView()->show();
        QCoreApplication::processEvents();
        m_sceneWidget->getView()->setTransform(viewTransform);
        m_sceneWidget->getView()->horizontalScrollBar()->setValue(m_scrollBarXValue);
        m_sceneWidget->getView()->verticalScrollBar()->setValue(m_scrollBarYValue);
      }
      else if (localName == "toolData") {
        PvlObject toolSettings;
        std::stringstream strStream( m_characterData.toStdString() );
        strStream >> toolSettings;
      }
    }
    
    m_characterData = "";

    return result;
  }


  /**
   * Draw elements
   *
   * If element is drawn in black, something is wrong. The correlation value is out of range.
   * 
   * Need separate redraw method because we will already have the list of MatrixSceneItems.
   *
   * @param corrMatrix The matrix that belongs to the project.
   */
  void MatrixSceneWidget::drawElements(CorrelationMatrix *corrMatrix) {
    int elementSize = 10;
    int startX = 20;
    int x = 20;
    int startY = 20;
    int y = 20;
    int yOffset = 0;
    QList<QGraphicsRectItem *> squares;
    QGraphicsRectItem *rectangle;
    QList<int> paramList;

    // This will change depending on the values of the elements
    QBrush fillBrush(Qt::blue);
    QPen outlinePen(Qt::black);
    outlinePen.setWidth(0);

    //Make another iterator for the imagesAndParameters qmap and go from there...
//     QMapIterator<QString, QStringList> img1Iterator( *corrMatrix->imagesAndParameters() );
//     QMapIterator<QString, QStringList> img2Iterator( *corrMatrix->imagesAndParameters() );
    
    foreach ( SparseBlockColumnMatrix blockColumn, *( corrMatrix->visibleBlocks() ) ) {
      QMapIterator<int, matrix<double>*> block(blockColumn);
      bool lastBlock = true;
      block.toBack(); // moves iterator to AFTER the last item
//       img2Iterator.toBack();
//       img1Iterator.next();

      do {
        block.previous(); // go to last item
//         img2Iterator.previous();
        for (int row = 0; row < (int)block.value()->size1(); row++) {
          for (int column = 0; column < (int)block.value()->size2(); column++) {

            // The values of this matrix should always be between 1 and -1.
            double lowerColorValue = fabs( ( *block.value() )(row, column) ) * 255.0;
            double red = 0;
            double green = 0;

            if (fabs( ( *block.value() )(row, column) ) < .5) {
              red = lowerColorValue * 2;
              green = 255;
            }
            else {              
              green = 255 - ( (lowerColorValue - 127.5) * 2 );
              red = 255;
            }
                            
            QColor fillColor(red, green, 0);

            bool draw = true;
            // Deal with diagonal.
            if (lastBlock) {
              if (column < row) {
                draw = false;
              }
              else if (column == row) {
                outlinePen.setColor(Qt::black);
                fillBrush.setColor(Qt::blue); // This will always be a single color
              }
              else {
                outlinePen.setColor(Qt::black);
                fillBrush.setColor(fillColor);
              }
            }
            else {
              outlinePen.setColor(Qt::black);
              fillBrush.setColor(fillColor);
            }

            // Should rectangle be a matrix element?
            if (draw) {
              rectangle = m_graphicsScene->addRect(x, y,                     // Start Position
                                                   elementSize, elementSize, // Rectangle Size
                                                   outlinePen, fillBrush);   // outline, fill options
//               QString img1 = "Image 1: " + img1Iterator.key() + "\n";
//               QString param1 = "Parameter 1: " + img1Iterator.value().at(row) + "\n";
//               QString img2 = "Image 2: " + img2Iterator.key() + "\n";
//               QString param2 = "Parameter 2: " + img2Iterator.value().at(column);
//               QString toolTip = QString::number( ( *block.value() )(row, column) ) + "\n"
//                                 + img1 + param1 + img2 + param2;
              QString toolTip;
              rectangle->setToolTip(toolTip.setNum( ( *block.value() )(row, column) ) );
            }
            x += elementSize;
          }
          x = startX;
          y += elementSize;
        }
        x = startX;
        // jump up by the number of rows in the previous block
        if ( block.hasPrevious() ) {
          yOffset += block.peekPrevious().value()->size1() * elementSize;
        }
        y = startY - yOffset;
        lastBlock = false;
      } while ( block.hasPrevious() );
      
      //reset second img iterator
//       img2Iterator.toBack();

      startX += block.value()->size2() * elementSize;
      startY += block.value()->size1() * elementSize;
      x = startX;
      y = startY;
      yOffset = 0;
    }
  }



  /**
   * Draw the lines that will show the columns and blocks for each image.
   *
   * @param matrix 
   */
  void MatrixSceneWidget::drawGrid(CorrelationMatrix *corrMatrix) {
    int startVX = 20;
    int startVY = 20;
    int startHX = 20;
    int startHY = 20;
    
    int elementSize = 10;
    
    QList<int> segments;
    int segmentLength = 0;

    QMapIterator<QString, QStringList> it( *corrMatrix->imagesAndParameters() );
    // get list of segment lengths1
    while ( it.hasNext() ) {
      it.next();
      int numOfParams = it.value().count();
      segmentLength += numOfParams * elementSize;
      segments.append(segmentLength);
    }

    segments.removeLast();
    
    QList<QGraphicsLineItem> lines;

    QPen pen;
    pen.setColor(Qt::black);
    pen.setWidth(1);

    // Top of matrix, at this point segmentLength is the length of the longest side
    QGraphicsLineItem *hLine;
    hLine = new QGraphicsLineItem(startHX, startHY,
                                  startHX + segmentLength, startHY);
    hLine->setPen(pen);
    m_graphicsScene->addItem(hLine);

    // Right most side of matrix (edge)
    QGraphicsLineItem *vLine;
    vLine = new QGraphicsLineItem(startVX + segmentLength, startVY,
                                  startVX + segmentLength, startVY + segmentLength);
    vLine->setPen(pen);
    m_graphicsScene->addItem(vLine);

    int edge = startVX + segmentLength;
    int currentLength = segmentLength;
    int currentVX = startVX;

    //draw the rest of the grid
    foreach (int segment, segments) {
      currentVX = startVX + segment;
      currentLength = segmentLength - segment;

      vLine = new QGraphicsLineItem(currentVX, startVY,
                                    currentVX, startVY + segment);

      hLine = new QGraphicsLineItem(edge - currentLength, startHY + segment,
                                    edge, startHY + segment);

      vLine->setPen(pen);
      hLine->setPen(pen);
      m_graphicsScene->addItem(vLine);
      m_graphicsScene->addItem(hLine);
    }
  }



  /**
   * Redraw matrix when the focus is chagned.
   *
   * @param
   */
//   void MatrixSceneWidget::redrawElements(newParameters) {
//     // Create MatrixElements?
//     // Later this will be called from visibleElements(x, y) and visible elements will be called
//     // from here...
//     QList<*MatrixELement> elements = matrix.correlationMatrixFromFile();
//     // And create MatrixSceneItems and draw in same loop
//     foreach (MatrixELement *element, elements) {
//       MatrixSceneItem itemToDraw = MatrixSceneItem(element);
//       m_matrixSceneItems.append(itemToDraw);
//       m_graphicsScene->addItem(itemToDraw);
//     }
//   }



  /**
   * Change item colors when options are changed.
   *
   * @param colorScheme True if using tolerance. False if using gradient.
   */
  void MatrixSceneWidget::repaintItems(bool colorScheme) {
//     if (colorScheme) {
//       // dont actually use this. just redo the colors?
//       drawElements( m_directory->project()->correlationMatrix() );
//     }
//     else {
//       foreach ( MatrixElement element, m_graphicsScene->items() ) {
//         if ( element.correlation() >= m_options->tolerance() ) {
//           element.setColor( m_options->badCorrelationColor() );
//         }
//         else {
//           element.setColor( m_options->goodCorrelationColor() );
//         }
//       }
//     }
  }
}
