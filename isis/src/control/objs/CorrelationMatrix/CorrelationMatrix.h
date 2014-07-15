#ifndef CorrelationMatrix_h
#define CorrelationMatrix_h

/**
 * @file
 *   Unless noted otherwise, the portions of Isis written by the USGS are public
 *   domain. See individual third-party library and package descriptions for
 *   intellectual property information,user agreements, and related information.
 *
 *   Although Isis has been used by the USGS, no warranty, expressed or implied,
 *   is made by the USGS as to the accuracy and functioning of such software
 *   and related material nor shall the fact of distribution constitute any such
 *   warranty, and no responsibility is assumed by the USGS in connection
 *   therewith.
 *
 *   For additional information, launch
 *   $ISISROOT/doc//documents/Disclaimers/Disclaimers.html in a browser or see
 *   the Privacy &amp; Disclaimers page on the Isis website,
 *   http://isis.astrogeology.usgs.gov, and the USGS privacy and disclaimers on
 *   http://www.usgs.gov/privacy.html.
 */

#include "FileName.h"

#include <QDebug>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "boost/numeric/ublas/matrix_sparse.hpp"

template <typename A, typename B> class QMap;
template <typename A> class QList;

namespace Isis {
  class FileName;
  class MosaicSceneWidget;
  class PvlObject;
  class SparseBlockColumnMatrix;
  /**
   * @brief This is a container for the correlation matrix that comes from a bundle adjust
   *
   * The bundle adjust will output the covariance matrix to a file. This class will read that file
   * in and compute the correlation matrix. The entire correlation matrix will be written to a file
   * and values will be read/displayed on an as-needed basis.
   *
   * @ingroup Visualization Tools
   *
   * @author 2014-05-02 Kimberly Oyama
   *
   * @internal
   */
  class CorrelationMatrix {
    public:
      CorrelationMatrix();
      CorrelationMatrix(PvlObject storedMatrixData);
      CorrelationMatrix(const CorrelationMatrix &other);
      ~CorrelationMatrix();
      
      CorrelationMatrix &operator=(const CorrelationMatrix &other);

      void computeCorrelationMatrix();
      void retrieveVisibleElements(int x, int y);

      bool isValid();

      void setCorrelationFileName(FileName correlationFileName);
      void setCovarianceFileName(FileName covarianceFileName);
      void setImagesAndParameters(QMap<QString, QStringList> *imagesAndParameters);

      SparseBlockColumnMatrix correlationMatrixFromFile(QDataStream inStream);
      //might need something called deleteLater(), called from MatrixTreeWidgetItem constructor.

      //if cov filename is null we need to ask the user to find it.

      FileName correlationFileName();
      FileName covarianceFileName();
      QMap<QString, QStringList> *imagesAndParameters();

      void getWholeMatrix();
      void getThreeVisibleBlocks();

      // Need these for range used to pick colors....
      QList<SparseBlockColumnMatrix> *visibleBlocks();

      PvlObject pvlObject();

    private:
      //! This map holds the images used to create this matrix and their associated parameters.
      QMap<QString, QStringList> *m_imagesAndParameters;

      //! This map holds the images and parameters associated with the block in focus.
      QMap<QString, QStringList> *m_visibleImagesAndParameters;

      //! FileName of the covariance matrix calculated when the bundle was run.
      FileName *m_covarianceFileName;

      //! FileName of the correlation matrix
      FileName *m_correlationFileName;

      /**
       * List of the parameter values. Stored so we don't need to store all the SBCMs when
       * calculating the correlation values.
       */
      QList<double> *m_diagonals;

      /**
       * This will be the three blocks (or whole matrix depending on size) that apply to
       * the given area.
       */
      QList<SparseBlockColumnMatrix> *m_visibleBlocks;
  };
};

#endif
