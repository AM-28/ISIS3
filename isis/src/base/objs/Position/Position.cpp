#include "Position.h"

#include <algorithm>
#include <cfloat>
#include <iomanip>

#include <QString>
#include <QStringList>
#include <QChar>

#include "BasisFunction.h"
#include "IException.h"
#include "LeastSquares.h"
#include "LineEquation.h"
#include "NaifStatus.h"
#include "NumericalApproximation.h"
#include "PolynomialUnivariate.h"
#include "TableField.h"

namespace Isis {
  /**
   * Construct an empty Position class using valid body codes.
   * See required reading
   * ftp://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/ascii/individual_docs/naif_ids.req
   *
   * @param targetCode Valid naif body name.
   * @param observerCode Valid naif body name.
   */
  Position::Position(int targetCode, int observerCode) {
    init(targetCode, observerCode, false);
  }

/**
 * @brief Constructor for swapping observer/target order
 *
 * This specialized constructor is provided to expressly support swap of
 * observer/target order in the NAIF spkez_c/spkezp_c routines when determining
 * the state of spacecraft and target body.
 *
 * Traditionally, ISIS3 (and ISIS2!) has had the target/observer order swapped
 * improperly to determine state vector between the two bodies.  This
 * constructor provides a transaitional path to correct this in a
 * controlled way.  See SpacecraftPosition for details.
 *
 * Note that the targetCode and observerCode are always provided in the same
 * order as the orignal constructor (i.e., targetCode=s/c, observerCode=planet)
 * as the swapObserverTarget boolean parameter provides the means to properly
 * implement the swap internally.
 *
 * It is not as simple as simply swapping the codes in the constructor as
 * internal representation of the state vector is in body fixed state of the
 * target body relative to observer.  If the swap is invoked, the state vector
 * must be inverted.
 *
 * @author 2012-10-29 Kris Becker
 *
 * @param targetCode         Traditional s/c code
 * @param observerCode       Traditional target/planet code
 * @param swapObserverTarget True indicates implement the observer/target swap,
 *                             false will invoke preexisting behavior
 */
  Position::Position(int targetCode, int observerCode,
                               bool swapObserverTarget) {
    init(targetCode, observerCode, swapObserverTarget);
  }

/**
 * @brief Internal initialization of the object support observer/target swap
 *
 * This initailizer provides options to swap observer/target properly.
 *
 * @author 2012-10-29 Kris Becker
 *
 * @param targetCode         Traditional s/c code
 * @param observerCode       Traditional target/planet code
 * @param swapObserverTarget True indicates implement the observer/target swap,
 *                             false will invoke preexisting behavior
 */
  void Position::init(int targetCode, int observerCode,
                           const bool &swapObserverTarget) {

    p_aberrationCorrection = "LT+S";
    p_baseTime = 0.;
    p_coefficients[0].clear();
    p_coefficients[1].clear();
    p_coefficients[2].clear();
    p_coordinate.resize(3);
    p_degree = 2;
    p_degreeApplied = false;
    p_et = -DBL_MAX;
    p_fullCacheStartTime = 0;
    p_fullCacheEndTime = 0;
    p_fullCacheSize = 0;
    p_hasVelocity = false;

    p_override = NoOverrides;
    p_source = Spice;

    p_timeBias = 0.0;
    p_timeScale = 1.;
    p_velocity.resize(3);
    p_xhermite = NULL;
    p_yhermite = NULL;
    p_zhermite = NULL;

    m_swapObserverTarget = swapObserverTarget;
    m_lt = 0.0;

    // Determine observer/target ordering
    if ( m_swapObserverTarget ) {
      // New/improved settings.. Results in vector negation in SetEphemerisTimeSpice
      p_targetCode = observerCode;
      p_observerCode = targetCode;
    }
    else {
      // Traditional settings
      p_targetCode = targetCode;
      p_observerCode = observerCode;
    }
  }


  /**
   * Free the memory allocated by this Position instance
   */
  Position::~Position() {
    ClearCache();
  }


  /**
   *  @brief Apply a time bias when invoking SetEphemerisTime method.
   *
   * The bias is used only when reading from NAIF kernels.  It is added to the
   * ephermeris time passed into SetEphemerisTime and then the body
   * position is read from the NAIF kernels and returned.  When the cache
   * is loaded from a table the bias is ignored as it is assumed
   * to have already been applied.  If this method is never called
   * the default bias is 0.0 seconds.
   *
   * @param timeBias time bias in seconds
   */
  void Position::SetTimeBias(double timeBias) {
    p_timeBias = timeBias;
  }

/**
 * @brief Returns the value of the time bias added to ET
 *
 * @author 2012-10-29 Kris Becker
 *
 * @return double Returns time bias for current object
 */
  double Position::GetTimeBias() const {
    return (p_timeBias);
  }

  /**
   *  @brief Set the aberration correction (light time)
   *
   * See NAIF required reading for more information on this correction at
   * ftp://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/ascii/individual_docs/spk.req
   *
   * @param correction  This value must be one of the following: "NONE", "LT",
   * "LT+S", "CN", "CN+S", "XLT", "XLT+S", "XCN" and "XCN+S" where LT is a
   * correction for planetary aberration (light time), CN is converged Newtonian,
   * XLT is transmission case using Newtonian formulation, XCN is transmission
   * case using converged Newtonian formuation and S a correction for stellar
   * aberration. See
   * http://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/cspice/spkezr_c.html for
   * additional information on the "abcorr" parameters.  If never called the
   * default is "LT+S".
   *
   * @internal
   * @history 2012-10-10 Kris Becker - Added support for all current abcorr
   *          parameters options in the NAIF spkezX functions.
   *
   */
  void Position::SetAberrationCorrection(const QString &correction) {
    QString abcorr(correction);
    abcorr.remove(QChar(' '));
    abcorr = abcorr.toUpper();
    QStringList validAbcorr;
    validAbcorr << "NONE" << "LT" << "LT+S" << "CN" << "CN+S"
                << "XLT" << "XLT+S" << "XCN" << "XCN+S";

    if (validAbcorr.indexOf(abcorr) >= 0) {
      p_aberrationCorrection = abcorr;
    }
    else {
      QString msg = "Invalid abberation correction [" + correction + "]";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }
  }

/**
 * @brief Returns current state of stellar aberration correction
 *
 * The aberration correction is the value of the parameter that will be
 * provided to the spkez_c/spkezp_c routines when determining the
 * targer/observer vector state.  See SetAberrationCorrection() for valid
 * values.
 *
 * @author 2012-10-29 Kris Becker
 */
  QString Position::GetAberrationCorrection() const {
    return (p_aberrationCorrection);
  }

/**
 * @brief Return the light time coorection value
 *
 * This method returns the light time correction of the last call to
 * SetEphemerisTimeSpice.  This is the actual time after adjustments that has
 * resulting int the adjustment of the target body.  The observer position
 * should be unchanged.
 *
 * @author 2012-10-29 Kris Becker
 *
 * @return double Returns the light time determined from the last call to
 *         SetEphemerisTime[Spice].
 */
  double Position::GetLightTime() const {
    return (m_lt);
  }

  /** Return J2000 coordinate at given time.
   *
   * This method returns the J2000 coordinates (x,y,z) of the body at a given
   * et in seconds.  The coordinates are obtained from either a valid NAIF spk
   * kernel, or alternatively from an internal cache loaded from an ISIS Table
   * object.  In the first case, the SPK kernel must contain positions for
   * the body code specified in the constructor at the given time and it must
   * be loaded using the SpiceKernel class.
   *
   * @param et   ephemeris time in seconds
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Moved code to
   *                      individual methods for each Source type
   *                      to make software more readable.
   */
  std::vector<std::vector<double>> Position::SetEphemerisTime(double et) {
    NaifStatus::CheckErrors();
    std::vector<std::vector<double>> ephemerisData;

    // Save the time
    if (et == p_et) {
      ephemerisData = {p_coordinate, {0.0, 0.0, 0.0}};
      if (p_hasVelocity) {
        ephemerisData = {p_coordinate, p_velocity};
      }
      return ephemerisData;
    }
    p_et = et;

    // Read from the cache
    if(p_source == Memcache) {
      SetEphemerisTimeMemcache();
    }
    else if(p_source == HermiteCache) {
      SetEphemerisTimeHermiteCache();
    }
    else if(p_source == PolyFunction) {
      SetEphemerisTimePolyFunction();
    }
    else if(p_source == PolyFunctionOverHermiteConstant) {
      SetEphemerisTimePolyFunctionOverHermiteConstant();
    }
    else {  // Read from the kernel
      SetEphemerisTimeSpice();
    }

    NaifStatus::CheckErrors();

    // Return the coordinate
    ephemerisData = {p_coordinate, {0.0, 0.0, 0.0}};
    if (p_hasVelocity) {
      ephemerisData = {p_coordinate, p_velocity};
    }
    return ephemerisData;
  }


  /** Cache J2000 position over a time range.
   *
   * This method will load an internal cache with coordinates over a time
   * range.  This prevents
   * the NAIF kernels from being read over-and-over again and slowing a
   * application down due to I/O performance.  Once the
   * cache has been loaded then the kernels can be unloaded from the NAIF
   * system.
   *
   * @param startTime   Starting ephemeris time in seconds for the cache
   * @param endTime     Ending ephemeris time in seconds for the cache
   * @param size        Number of coordinates/positions to keep in the cache
   *
   */
  void Position::LoadCache(double startTime, double endTime, int size) {
    // Make sure cache isn't already loaded
    // ???? Do something here
    if(p_source == Memcache || p_source == HermiteCache) {
      QString msg = "A Position cache has already been created";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    if(startTime > endTime) {
      QString msg = "Argument startTime must be less than or equal to endTime";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    if((startTime != endTime) && (size == 1)) {
      QString msg = "Cache size must be more than 1 if startTime endTime differ";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    // Save full cache parameters
    p_fullCacheStartTime = startTime;
    p_fullCacheEndTime = endTime;
    p_fullCacheSize = size;
    LoadTimeCache();

    // Loop and load the cache
    for(int i = 0; i < size; i++) {
      double et = p_cacheTime[i];
      SetEphemerisTime(et);
      p_cache.push_back(p_coordinate);
      if(p_hasVelocity) p_cacheVelocity.push_back(p_velocity);
    }

    p_source = Memcache;
  }


  /** Cache J2000 position for a time.
   *
   * This method will load an internal cache with coordinates for a single
   * time (e.g. useful for framing cameras). This prevents
   * the NAIF kernels from being read over-and-over again and slowing a
   * application down due to I/O performance.  Once the
   * cache has been loaded then the kernels can be unloaded from the NAIF
   * system.  This calls the LoadCache(stime,etime,size) method using the
   * time as both the starting and ending time with a size of 1.
   *
   * @param time   single ephemeris time in seconds to cache
   *
   */
  void Position::LoadCache(double time) {
    LoadCache(time, time, 1);
  }


  void Position::LoadCache(json &isdPos) {
    if (p_source != Spice) {
        throw IException(IException::Programmer, "Position::LoadCache(json) only support Spice source", _FILEINFO_);
    }

    // Load the full cache time information from the label if available
    p_fullCacheStartTime = isdPos["SpkTableStartTime"].get<double>();
    p_fullCacheEndTime = isdPos["SpkTableEndTime"].get<double>();
    p_fullCacheSize = isdPos["SpkTableOriginalSize"].get<double>();
    p_cacheTime = isdPos["EphemerisTimes"].get<std::vector<double>>();

    for (auto it = isdPos["Positions"].begin(); it != isdPos["Positions"].end(); it++) {
      std::vector<double> pos = {it->at(0).get<double>(), it->at(1).get<double>(), it->at(2).get<double>()};
      p_cache.push_back(pos);
    }

    p_cacheVelocity.clear();

    bool hasVelocityKey = isdPos.find("Velocities") != isdPos.end();

    if (hasVelocityKey) {
      for (auto it = isdPos["Velocities"].begin(); it != isdPos["Velocities"].end(); it++) {
        std::vector<double> vel = {it->at(0).get<double>(), it->at(1).get<double>(), it->at(2).get<double>()};
        p_cacheVelocity.push_back(vel);
      }
    }

    p_hasVelocity = !p_cacheVelocity.empty();
    p_source = Memcache;

    SetEphemerisTime(p_cacheTime[0]);
  }

  /** Cache J2000 positions using a table file.
   *
   * This method will load an internal cache with coordinates from an
   * ISIS table file.  The table must have 4 columns,
   * or 7 (if velocity) is included, and at least one row. The 4
   * columns contain the following information, body position
   * x,y,z in J2000 and the ephemeris time of that position. If
   * there are multiple rows it is assumed you can interpolate
   * position at times in between the rows.
   *
   * @param table   An ISIS table blob containing valid J2000 coordinate/time
   *                values
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Reads CacheType
   *            keyword from table and sets p_source.  If no
   *            CacheType keyword, we know this is an older image,
   *            so set p_source to Memcache.
   *   @history 2011-01-05 Debbie A. Cook - Added PolyFunction type
   *   @history 2011-04-08 Debbie A. Cook - Corrected loop counter in
   *            PolyFunction section to only go up to table.Records() - 1
   *
   */
  void Position::LoadCache(Table &table) {

    // Make sure cache isn't alread loaded
    if(p_source == Memcache || p_source == HermiteCache) {
      QString msg = "A Position cache has already been created";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    // Load the full cache time information from the label if available
    if(table.Label().hasKeyword("SpkTableStartTime")) {
      p_fullCacheStartTime = toDouble(table.Label().findKeyword("SpkTableStartTime")[0]);
    }
    if(table.Label().hasKeyword("SpkTableEndTime")) {
      p_fullCacheEndTime = toDouble(table.Label().findKeyword("SpkTableEndTime")[0]);
    }
    if(table.Label().hasKeyword("SpkTableOriginalSize")) {
      p_fullCacheSize = toDouble(table.Label().findKeyword("SpkTableOriginalSize")[0]);
    }


    // set source type by table's label keyword
    if(!table.Label().hasKeyword("CacheType")) {
      p_source = Memcache;
    }
    else if(table.Label().findKeyword("CacheType")[0] == "Linear") {
      p_source = Memcache;
    }
    else if(table.Label().findKeyword("CacheType")[0] == "HermiteSpline") {
      p_source = HermiteCache;
      p_overrideTimeScale = 1.;
      p_override = ScaleOnly;
    }
    else if(table.Label().findKeyword("CacheType")[0] == "PolyFunction") {
      p_source = PolyFunction;
    }
    else {
      throw IException(IException::Io,
                       "Invalid value for CacheType keyword in the table "
                       + table.Name(),
                       _FILEINFO_);
    }

    // Loop through and move the table to the cache
    if (p_source != PolyFunction) {
      for (int r = 0; r < table.Records(); r++) {
        TableRecord &rec = table[r];
        if (rec.Fields() == 7) {
          p_hasVelocity = true;
        }
        else if (rec.Fields() == 4) {
          p_hasVelocity = false;
        }
        else  {
          QString msg = "Expecting four or seven fields in the Position table";
          throw IException(IException::Programmer, msg, _FILEINFO_);
        }

        std::vector<double> j2000Coord;
        j2000Coord.push_back((double)rec[0]);
        j2000Coord.push_back((double)rec[1]);
        j2000Coord.push_back((double)rec[2]);
        int inext = 3;

        p_cache.push_back(j2000Coord);
        if (p_hasVelocity) {
          std::vector<double> j2000Velocity;
          j2000Velocity.push_back((double)rec[3]);
          j2000Velocity.push_back((double)rec[4]);
          j2000Velocity.push_back((double)rec[5]);
          inext = 6;

          p_cacheVelocity.push_back(j2000Velocity);
        }
        p_cacheTime.push_back((double)rec[inext]);
      }
    }
    else {
      // Coefficient table for postion coordinates x, y, and z
      std::vector<double> coeffX, coeffY, coeffZ;

      for (int r = 0; r < table.Records() - 1; r++) {
        TableRecord &rec = table[r];

        if(rec.Fields() != 3) {
          // throw an error
        }
        coeffX.push_back((double)rec[0]);
        coeffY.push_back((double)rec[1]);
        coeffZ.push_back((double)rec[2]);
      }
      // Take care of function time parameters
      TableRecord &rec = table[table.Records()-1];
      double baseTime = (double)rec[0];
      double timeScale = (double)rec[1];
      double degree = (double)rec[2];
      SetPolynomialDegree((int) degree);
      SetOverrideBaseTime(baseTime, timeScale);
      SetPolynomial(coeffX, coeffY, coeffZ);
      if (degree > 0)  p_hasVelocity = true;
      if(degree == 0  && p_cacheVelocity.size() > 0) p_hasVelocity = true;
    }
  }


  /** Return a table with J2000 positions.
   *
   * Return a table containg the cached coordinates with the given name.  The
   * table will have four or seven columns, J2000 x,y,z (optionally vx,vy,vx)
   * and the ephemeris time.
   *
   * @param tableName    Name of the table to create and return
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Added CacheType
   *            keyword to output table.  This is based on
   *            p_source and will be read by LoadCache(Table).
   *   @history 2011-01-05 Debbie A. Cook - Added PolyFunction
   */
  Table Position::Cache(const QString &tableName) {
    if (p_source == PolyFunctionOverHermiteConstant) {
      LineCache(tableName);
      // TODO Figure out how to get the tolerance -- for now hard code .01
      Memcache2HermiteCache(0.01);

      //std::cout << "Cache size is " << p_cache.size();

    }

    // record to be added to table
    TableRecord record;

    if (p_source != PolyFunction) {
    // add x,y,z position labels to record
      TableField x("J2000X", TableField::Double);
      TableField y("J2000Y", TableField::Double);
      TableField z("J2000Z", TableField::Double);
      record += x;
      record += y;
      record += z;

      if (p_hasVelocity) {
      // add x,y,z velocity labels to record
        TableField vx("J2000XV", TableField::Double);
        TableField vy("J2000YV", TableField::Double);
        TableField vz("J2000ZV", TableField::Double);
        record += vx;
        record += vy;
        record += vz;
      }
    // add time label to record
      TableField t("ET", TableField::Double);
      record += t;

    // create output table
      Table table(tableName, record);

      int inext = 0;

      for (int i = 0; i < (int)p_cache.size(); i++) {
        record[inext++] = p_cache[i][0];                     // record[0]
        record[inext++] = p_cache[i][1];                     // record[1]
        record[inext++] = p_cache[i][2];                     // record[2]
        if (p_hasVelocity) {
          record[inext++] = p_cacheVelocity[i][0];           // record[3]
          record[inext++] = p_cacheVelocity[i][1];           // record[4]
          record[inext++] = p_cacheVelocity[i][2];           // record[5]
        }
        record[inext] = p_cacheTime[i];                      // record[6]
        table += record;

        inext = 0;
      }
      CacheLabel(table);
      return table;
    }

    else if(p_source == PolyFunction  &&  p_degree == 0  &&  p_fullCacheSize == 1)
      // Just load the position for the single epoch
      return LineCache(tableName);

    // Load the coefficients for the curves fit to the 3 camera angles
    else if (p_source == PolyFunction) {
      // PolyFunction case
      TableField spacecraftX("J2000SVX", TableField::Double);
      TableField spacecraftY("J2000SVY", TableField::Double);
      TableField spacecraftZ("J2000SVZ", TableField::Double);

      TableRecord record;
      record += spacecraftX;
      record += spacecraftY;
      record += spacecraftZ;

      Table table(tableName, record);

      for(int cindex = 0; cindex < p_degree + 1; cindex++) {
        record[0] = p_coefficients[0][cindex];
        record[1] = p_coefficients[1][cindex];
        record[2] = p_coefficients[2][cindex];
        table += record;
      }

      // Load one more table entry with the time adjustments for the fit equation
      // t = (et - baseTime)/ timeScale
      record[0] = p_baseTime;
      record[1] = p_timeScale;
      record[2] = (double) p_degree;

      CacheLabel(table);
      table += record;
      return table;
    }

    else {
      throw IException(IException::Io,
                       "Cannot create Table, no Cache is loaded.",
                       _FILEINFO_);
    }

  }



  /** Add labels to a Position table.
   *
   * Return a table containing the labels defining the position table.
   *
   * @param Table    Table to receive labels
   */
  void Position::CacheLabel(Table &table) {
    // determine type of table to return
    QString tabletype = "";

    if(p_source == Memcache) {
      tabletype = "Linear";
    }
    else if(p_source == HermiteCache) {
      tabletype = "HermiteSpline";
    }
    else {
      tabletype = "PolyFunction";
    }

    table.Label() += PvlKeyword("CacheType", tabletype);

    // Write original time coverage
    if(p_fullCacheStartTime != 0) {
      table.Label() += PvlKeyword("SpkTableStartTime");
      table.Label()["SpkTableStartTime"].addValue(toString(p_fullCacheStartTime));
    }
    if(p_fullCacheEndTime != 0) {
      table.Label() += PvlKeyword("SpkTableEndTime");
      table.Label()["SpkTableEndTime"].addValue(toString(p_fullCacheEndTime));
    }
    if(p_fullCacheSize != 0) {
      table.Label() += PvlKeyword("SpkTableOriginalSize");
      table.Label()["SpkTableOriginalSize"].addValue(toString(p_fullCacheSize));
    }
  }



  /** Return a table with J2000 to reference positions.
   *
   * Return a table containing the cached positions with the given
   * name. The table will have seven columns, positionX, positionY,
   * positionZ, angular velocity X, angular velocity Y, angular
   * velocity Z, and time of J2000 position.
   *
   * @param tableName    Name of the table to create and return
   *
   * @internal
   *   @history 2012-01-25 Debbie A. Cook - Modified error checking for p_source
   *                         to allow all function sources (>=HermiteCache)
   */
  Table Position::LineCache(const QString &tableName) {

    // Apply the function and fill the caches
    if(p_source >= HermiteCache)  ReloadCache();

    if(p_source != Memcache) {
      QString msg = "Only cached positions can be returned as a line cache of positions and time";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }
    // Load the table and return it to caller
    return Cache(tableName);
  }



  /** Cache J2000 positions over existing cached time range using polynomials
   *
   * This method will reload an internal cache with positions
   * calculated from functions fit to the coordinates of the position
   * over a time range.
   *
   * @internal
   *   @history 2012-01-25 Debbie A. Cook - Modified error checking for type
   *                        to allow all function types (>=HermiteCache)
   */
  void Position::ReloadCache() {
    NaifStatus::CheckErrors();

    // Save current et
    double et = p_et;

    // Make sure source is a function
    if(p_source < HermiteCache) {
      QString msg = "The Position has not yet been fit to a function";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    // Clear existing positions from thecache
    p_cacheTime.clear();
    p_cache.clear();

    // Clear the velocity cache if we can calculate it instead.  It can't be calculated for
    // functions of degree 0 (framing cameras), so keep the original velocity.  It is better than nothing.
    if (p_degree > 0  && p_cacheVelocity.size() > 1)  p_cacheVelocity.clear();

    // Load the time cache first
    LoadTimeCache();

    if (p_fullCacheSize > 1) {
    // Load the positions and velocity caches
    p_et = -DBL_MAX;   // Forces recalculation in SetEphemerisTime

      for (std::vector<double>::size_type pos = 0; pos < p_cacheTime.size(); pos++) {
        //        p_et = p_cacheTime.at(pos);
        SetEphemerisTime(p_cacheTime.at(pos));
        p_cache.push_back(p_coordinate);
        p_cacheVelocity.push_back(p_velocity);
      }
    }
    else {
    // Load the position for the single updated time instance
      p_et = p_cacheTime[0];
      SetEphemerisTime(p_et);
      p_cache.push_back(p_coordinate);
    }

    // Set source to cache and reset current et
    p_source = Memcache;
    p_et = -DBL_MAX;
    SetEphemerisTime(et);

    NaifStatus::CheckErrors();
  }


  /** Cache J2000 position over existing cached time range using
   *  polynomials stored as Hermite cubic spline knots
   *
   * This method will reload an internal cache with positions
   * formed from a cubic Hermite spline over a time range.  The
   * method assumes a polynomial function has been fit to the
   * coordinates of the positions and calculates the spline
   * from the polynomial function.
   *
   */
   Table Position::LoadHermiteCache(const QString &tableName) {
    // find the first and last time values
    double firstTime = p_fullCacheStartTime;
    double lastTime = p_fullCacheEndTime;
    int cacheTimeSize = (int) p_fullCacheSize;

    // Framing cameras are already cached and don't need to be reduced.
    if(cacheTimeSize == 1) return Cache(tableName);

    //If it's already a hermite cache, just return it.
    if (p_source == HermiteCache) {
      return Cache(tableName);
    }

    // Make sure a polynomial function is already loaded
    if(p_source != PolyFunction) {
      QString msg = "A Position polynomial function has not been created yet";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    // Load the polynomial functions
    Isis::PolynomialUnivariate function1(p_degree);
    Isis::PolynomialUnivariate function2(p_degree);
    Isis::PolynomialUnivariate function3(p_degree);
    function1.SetCoefficients(p_coefficients[0]);
    function2.SetCoefficients(p_coefficients[1]);
    function3.SetCoefficients(p_coefficients[2]);

    // Clear existing coordinates from cache
    ClearCache();

    // Set velocity vector to true since it is calculated
    p_hasVelocity = true;

//    std::cout <<"time cache size is " << p_cacheTime.size() << std::endl;

    // Calculate new postition coordinates from polynomials fit to coordinates &
    // fill cache
//    std::cout << "Before" << std::endl;
//    double savetime = p_cacheTime.at(0);
//    SetEphemerisTime(savetime);
//    std::cout << "     at time " << savetime << std::endl;
//    for (int i=0; i<3; i++) {
//      std::cout << p_coordinate[i] << std::endl;
//    }


    // find time for the extremum of each polynomial
    // since this is only a 2nd degree polynomial,
    // finding these extrema is simple
    double b1 = function1.Coefficient(1);
    double c1 = function1.Coefficient(2);
    double extremumXtime = -b1 / (2.*c1) + p_baseTime; // extremum is time value for root of 1st derivative
    // make sure we are in the domain
    if(extremumXtime < firstTime) {
      extremumXtime = firstTime;
    }
    if(extremumXtime > lastTime) {
      extremumXtime = lastTime;
    }

    double b2 = function2.Coefficient(1);
    double c2 = function2.Coefficient(2);
    double extremumYtime = -b2 / (2.*c2) + p_baseTime;
    // make sure we are in the domain
    if(extremumYtime < firstTime) {
      extremumYtime = firstTime;
    }
    if(extremumYtime > lastTime) {
      extremumYtime = lastTime;
    }

    double b3 = function3.Coefficient(1);
    double c3 = function3.Coefficient(2);
    double extremumZtime = -b3 / (2.*c3) + p_baseTime;
    // make sure we are in the domain
    if(extremumZtime < firstTime) {
      extremumZtime = firstTime;
    }
    if(extremumZtime > lastTime) {
      extremumZtime = lastTime;
    }

    // refill the time vector
    p_cacheTime.clear();
    p_cacheTime.push_back(firstTime);
    p_cacheTime.push_back(extremumXtime);
    p_cacheTime.push_back(extremumYtime);
    p_cacheTime.push_back(extremumZtime);
    p_cacheTime.push_back(lastTime);
    // we don't know order of extrema, so sort
    sort(p_cacheTime.begin(), p_cacheTime.end());
    // in case an extremum is an endpoint
    std::vector <double>::iterator it = unique(p_cacheTime.begin(), p_cacheTime.end());
    p_cacheTime.resize(it - p_cacheTime.begin());

    if(p_cacheTime.size() == 2) {
      p_cacheTime.clear();
      p_cacheTime.push_back(firstTime);
      p_cacheTime.push_back((firstTime + lastTime) / 2);
      p_cacheTime.push_back(lastTime);
    }

    // add positions and velocities for these times
    for(int i = 0; i < (int) p_cacheTime.size(); i++) {
      // x,y,z positions
      double time;
      time = p_cacheTime[i] - p_baseTime;
      p_coordinate[0] = function1.Evaluate(time);
      p_coordinate[1] = function2.Evaluate(time);
      p_coordinate[2] = function3.Evaluate(time);
      p_cache.push_back(p_coordinate);

      // x,y,z velocities
      p_velocity[0] = b1 + 2 * c1 * (p_cacheTime[i] - p_baseTime);
      p_velocity[1] = b2 + 2 * c2 * (p_cacheTime[i] - p_baseTime);
      p_velocity[2] = b3 + 2 * c3 * (p_cacheTime[i] - p_baseTime);
      p_cacheVelocity.push_back(p_velocity);
    }

    p_source = HermiteCache;
    double et = p_et;
    p_et = -DBL_MAX;
    SetEphemerisTime(et);

    return Cache(tableName);
  }


  /** Set the coefficients of a polynomial fit to each of the components (X, Y, Z)
   *  of the position vector for the time period covered by the cache,
   *  component = c0 + c1*t + c2*t**2 + ... + cn*t**n,
   *  where t = (time - p_baseTime) / p_timeScale.
   *
   */
  void Position::SetPolynomial(Source type) {
    std::vector<double> XC, YC, ZC;

    // Check to see if the position is already a Polynomial Function
    if (p_source == PolyFunction)
      return;

    // Adjust the degree of the polynomial to the available data
    if (p_cache.size() == 1) {
      p_degree = 0;
    }
    else if (p_cache.size() == 2) {
      p_degree = 1;
    }

    //Check for polynomial over Hermite constant and initialize coefficients
    if (type == PolyFunctionOverHermiteConstant) {
      XC.assign(p_degree + 1, 0.);
      YC.assign(p_degree + 1, 0.);
      ZC.assign(p_degree + 1, 0.);
      SetPolynomial(XC, YC, ZC, type);
      return;
    }

    Isis::PolynomialUnivariate function1(p_degree);       //!< Basis function fit to X
    Isis::PolynomialUnivariate function2(p_degree);       //!< Basis function fit to Y
    Isis::PolynomialUnivariate function3(p_degree);       //!< Basis function fit to Z

    // Compute the base time
    ComputeBaseTime();
    std::vector<double> time;

    if(p_cache.size() == 1) {
      double t = p_cacheTime.at(0);
      SetEphemerisTime(t);
      XC.push_back(p_coordinate[0]);
      YC.push_back(p_coordinate[1]);
      ZC.push_back(p_coordinate[2]);
    }
    else if(p_cache.size() == 2) {
// Load the times and get the corresponding coordinates
      double t1 = p_cacheTime.at(0);
      SetEphemerisTime(t1);
      std::vector<double> coord1 = p_coordinate;
      t1 = (t1 - p_baseTime) / p_timeScale;
      double t2 = p_cacheTime.at(1);
      SetEphemerisTime(t2);
      std::vector<double> coord2 = p_coordinate;
      t2 = (t2 - p_baseTime) / p_timeScale;
      double slope[3];
      double intercept[3];

// Compute the linear equation for each coordinate and save them
      for(int cIndex = 0; cIndex < 3; cIndex++) {
        Isis::LineEquation posline(t1, coord1[cIndex], t2, coord2[cIndex]);
        slope[cIndex] = posline.Slope();
        intercept[cIndex] = posline.Intercept();
      }
      XC.push_back(intercept[0]);
      XC.push_back(slope[0]);
      YC.push_back(intercept[1]);
      YC.push_back(slope[1]);
      ZC.push_back(intercept[2]);
      ZC.push_back(slope[2]);
    }
    else {
      LeastSquares *fitX = new LeastSquares(function1);
      LeastSquares *fitY = new LeastSquares(function2);
      LeastSquares *fitZ = new LeastSquares(function3);

      // Load the known values to compute the fit equation
      for(std::vector<double>::size_type pos = 0; pos < p_cacheTime.size(); pos++) {
        double t = p_cacheTime.at(pos);
        time.push_back( (t - p_baseTime) / p_timeScale);
        SetEphemerisTime(t);
        std::vector<double> coord = p_coordinate;

        fitX->AddKnown(time, coord[0]);
        fitY->AddKnown(time, coord[1]);
        fitZ->AddKnown(time, coord[2]);
        time.clear();
      }
      //Solve the equations for the coefficients
      fitX->Solve();
      fitY->Solve();
      fitZ->Solve();

      // Delete the least squares objects now that we have all the coefficients
      delete fitX;
      delete fitY;
      delete fitZ;

      // For now assume all three coordinates are fit to a polynomial function.
      // Later they may each be fit to a unique basis function.
      // Fill the coefficient vectors

      for(int i = 0;  i < function1.Coefficients(); i++) {
        XC.push_back(function1.Coefficient(i));
        YC.push_back(function2.Coefficient(i));
        ZC.push_back(function3.Coefficient(i));
      }

    }

    // Now that the coefficients have been calculated set the polynomial with them
    SetPolynomial(XC, YC, ZC);
    return;
  }



  /** Set the coefficients of a polynomial (parabola) fit to
   * each of the three coordinates of the position vector for the
   * time period covered by the cache, coord = c0 + c1*t + c2*t**2 + ... + cn*t**n,
   * where t = (time - p_baseTime) / p_timeScale.
   *
   * @param [in] XC Coefficients of fit to X coordinate
   * @param [in] YC Coefficients of fit to Y coordinate
   * @param [in] ZC Coefficients of fit to Z coordinate
   *
   * @internal
   *   @history 2012-02-05 Debbie A. Cook - Added type argument
   */
  void Position::SetPolynomial(const std::vector<double>& XC,
                                    const std::vector<double>& YC,
                                    const std::vector<double>& ZC,
                                    const Source type) {

    Isis::PolynomialUnivariate function1(p_degree);
    Isis::PolynomialUnivariate function2(p_degree);
    Isis::PolynomialUnivariate function3(p_degree);

    // Load the functions with the coefficients
    function1.SetCoefficients(XC);
    function2.SetCoefficients(YC);
    function3.SetCoefficients(ZC);

    // Compute the base time
    ComputeBaseTime();

    // Save the current coefficients
    p_coefficients[0] = XC;
    p_coefficients[1] = YC;
    p_coefficients[2] = ZC;

    // Set the flag indicating p_degree has been applied to the spacecraft
    // positions and the coefficients of the polynomials have been saved.
    p_degreeApplied = true;

    // Reset the interpolation source
    p_source = type;

    // Update the current position
    double et = p_et;
    p_et = -DBL_MAX;
    SetEphemerisTime(et);

    return;
  }



  /**
   *  Return the coefficients of a polynomial fit to each of the
   *  three coordinates of the position for the time period covered by the cache,
   *  angle = c0 + c1*t + c2*t**2 + ... + cn*t**n, where t = (time - p_basetime) / p_timeScale.
   *
   * @param [out] XC Coefficients of fit to first coordinate of position
   * @param [out] YC Coefficients of fit to second coordinate of position
   * @param [out] ZC Coefficients of fit to third coordinate of position
   *
   */
  void Position::GetPolynomial(std::vector<double>& XC,
                                    std::vector<double>& YC,
                                    std::vector<double>& ZC) {
    XC = p_coefficients[0];
    YC = p_coefficients[1];
    ZC = p_coefficients[2];

    return;
  }



  //! Compute the base time using cached times
  void Position::ComputeBaseTime() {
    if(p_override == NoOverrides) {
      p_baseTime = (p_cacheTime.at(0) + p_cacheTime.at(p_cacheTime.size() - 1)) / 2.;
      p_timeScale = p_baseTime - p_cacheTime.at(0);
    }
    else if (p_override == ScaleOnly) {
      p_baseTime = (p_cacheTime.at(0) + p_cacheTime.at(p_cacheTime.size() - 1)) / 2.;
      p_timeScale = p_overrideTimeScale;
    }
    else {
      p_baseTime = p_overrideBaseTime;
      p_timeScale = p_overrideTimeScale;
    }

    // Take care of case where 1st and last times are the same
    if(p_timeScale == 0)  p_timeScale = 1.0;
    return;
  }


  /**
   * Set an override base time to be used with observations on scanners to allow
   * all images in an observation to use the same base time and polynomials for
   * the positions.
   *
   * @param [in] baseTime The baseTime to use and override the computed base time
   *
   * @internal
   *   @history 2011-04-08 Debbie A. Cook - Corrected p_override set to
   *                         BaseAndScale
   */
  void Position::SetOverrideBaseTime(double baseTime, double timeScale) {
    p_overrideBaseTime = baseTime;
    p_overrideTimeScale = timeScale;
    p_override = BaseAndScale;
    return;
  }



  /** Set the coefficients of a polynomial fit to each of the
   *  three coordinates of the position vector for the time period covered by
   *  the cache,
   *
   *  coordinate = c0 + c1*t + c2*t**2 + ... cn*t**n, where t = (time - p_basetime) / p_timeScale.
   *
   * @param partialVar     Designated variable of the partial derivative
   * @return               Derivative of j2000 vector calculated with polynomial
   *                              with respect to partialVar
   *
   */
  std::vector<double> Position::CoordinatePartial(
         Position::PartialType partialVar, int coeffIndex) {
    // Start with a zero vector since the derivative of the other coordinates
    // with respect to the partial var will be 0.
    std::vector<double> coordinate(3, 0);

    // Get the index of the coordinate to update with the partial derivative
    int coordIndex = partialVar;

//  if(coeffIndex > 2) {
//    QString msg = "Position only supports up to a 2nd order fit for the spacecraft position";
//    throw IException(IException::Programmer, msg, _FILEINFO_);
//  }
//
    // Reset the coordinate to its derivative
    coordinate[coordIndex] = DPolynomial(coeffIndex);
    return coordinate;
  }



  /** Compute the derivative of the velocity with respect to the specified
   *  variable.  The velocity is the derivative of the coordinate with respect
   *  to time.
   *
   *  coordinate = C0 + C1*t + C2*t**2 + ... +Cn*t**n ,
   *    where t = (time - p_basetime) / p_timeScale.
   *  velocity = (1/p_timeScale) * (C1 + 2*C2*t + ... + n*Cn*t**(n-1))
   *  partial(velocity) with respect to C0 = 0.
   *  partial(velocity) with respect to C1 = 1/p_timeScale.
   *  partial(velocity) with respect to C2 = 2*t/p_timeScale
   *  .
   *  .
   *  .
   *  partial(velocity) with respect to CN = n*t**(n-1)/p_timeScale
   *
   * @param partialVar     Designated variable of the partial derivative
   * @return               Derivative of j2000 velocity vector calculated with
   *                        respect to partialVar
   *
   */
  std::vector<double> Position::VelocityPartial(
                    Position::PartialType partialVar, int coeffIndex) {
    // Start with a zero vector since the derivative of the other coordinates with
    // respect to the partial var will be 0.
    std::vector<double> dvelocity(3, 0);

    // Get the index of the coordinate to update with the partial derivative
    int coordIndex = partialVar;

    double time = (p_et - p_baseTime) / p_timeScale;
    double derivative = 0.;

    // Handle arithmetic failures
    double Epsilon = 1.0e-15;
    if (fabs(time) <= Epsilon) time = 0.0;

    // Only compute for coefficient index > 0 to avoid problems like 0 ** -1
    if (coeffIndex   > 0)
      derivative = coeffIndex * pow(time, coeffIndex-1) / p_timeScale;

    // Reset the velocity coordinate to its derivative
    dvelocity[coordIndex] = derivative;
    return dvelocity;
  }


  /**
   *  Evaluate the derivative of the fit polynomial (parabola) defined by the
   *  given coefficients with respect to the coefficient at the given index, at
   *  the current time.
   *
   * @param coeffIndex Index of coefficient to differentiate with respect
   *                         to
   * @return The derivative evaluated at the current time
   *
   */
  double Position::DPolynomial(const int coeffIndex) {
    double derivative = 1.;
    double time = (p_et - p_baseTime) / p_timeScale;

    if(coeffIndex > 0  && coeffIndex <= p_degree) {
      derivative = pow(time, coeffIndex);
    }
    else if(coeffIndex == 0) {
      derivative = 1;
    }
    else {
      QString msg = "Unable to evaluate the derivative of the SPICE position fit polynomial for "
                    "the given coefficient index [" + toString(coeffIndex) + "]. "
                    "Index is negative or exceeds degree of polynomial ["
                    + toString(p_degree) + "]";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }

    return derivative;
  }

  /** Return the velocity vector if available.
   *
   * @return The velocity vector evaluated at the current time
   */
  const std::vector<double> &Position::Velocity() {
    if(p_hasVelocity) {
      return p_velocity;
    }
    else {
      QString msg = "No velocity vector available";
      throw IException(IException::Programmer, msg, _FILEINFO_);
    }
  }



  /**
   * This is a protected method that is called by
   * SetEphemerisTime() when Source type is Memcache.  It
   * calculates J2000 coordinates (x,y,z) of the body that
   * correspond to a given et in seconds. These coordinates are
   * obtained from an internal cache loaded from an ISIS Table
   * object.
   * @see SetEphemerisTime()
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Original version
   *            (moved code from SetEphemerisTime() to its own
   *            method)
   */
  void Position::SetEphemerisTimeMemcache() {
    // If the cache has only one position return it
    if(p_cache.size() == 1) {
      p_coordinate[0] = p_cache[0][0];
      p_coordinate[1] = p_cache[0][1];
      p_coordinate[2] = p_cache[0][2];
      if(p_hasVelocity) {
        p_velocity[0] = p_cacheVelocity[0][0];
        p_velocity[1] = p_cacheVelocity[0][1];
        p_velocity[2] = p_cacheVelocity[0][2];
      }
    }

    else {
      // Otherwise determine the interval to interpolate
      std::vector<double>::iterator pos;
      pos = upper_bound(p_cacheTime.begin(), p_cacheTime.end(), p_et);

      int cacheIndex;
      if(pos != p_cacheTime.end()) {
        cacheIndex = distance(p_cacheTime.begin(), pos);
        cacheIndex--;
      }
      else {
        cacheIndex = p_cacheTime.size() - 2;
      }

      if(cacheIndex < 0) cacheIndex = 0;

      // Interpolate the coordinate
      double mult = (p_et - p_cacheTime[cacheIndex]) /
                    (p_cacheTime[cacheIndex+1] - p_cacheTime[cacheIndex]);
      std::vector<double> p2 = p_cache[cacheIndex+1];
      std::vector<double> p1 = p_cache[cacheIndex];

      p_coordinate[0] = (p2[0] - p1[0]) * mult + p1[0];
      p_coordinate[1] = (p2[1] - p1[1]) * mult + p1[1];
      p_coordinate[2] = (p2[2] - p1[2]) * mult + p1[2];

      if(p_hasVelocity) {
        p2 = p_cacheVelocity[cacheIndex+1];
        p1 = p_cacheVelocity[cacheIndex];
        p_velocity[0] = (p2[0] - p1[0]) * mult + p1[0];
        p_velocity[1] = (p2[1] - p1[1]) * mult + p1[1];
        p_velocity[2] = (p2[2] - p1[2]) * mult + p1[2];
      }
    }

  }



  /**
   * This is a protected method that is called by
   * SetEphemerisTime() when Source type is HermiteCache.  It
   * calculates J2000 coordinates (x,y,z) of the body that
   * correspond to a given et in seconds. These coordinates are
   * obtained by using a Hermite spline to interpolate values from
   * an internal reduced cache loaded from an ISIS Table object.
   * @see SetEphemerisTime()
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Original version
   */
  void Position::SetEphemerisTimeHermiteCache() {

    // what if framing camera???

    // On the first SetEphemerisTime, create our splines. Later calls should
    // reuse these splines.
    if(p_xhermite == NULL) {
      p_overrideTimeScale = 1.;
      p_override = ScaleOnly;
      ComputeBaseTime();

      p_xhermite = new NumericalApproximation(
          NumericalApproximation::CubicHermite);
      p_yhermite = new NumericalApproximation(
          NumericalApproximation::CubicHermite);
      p_zhermite = new NumericalApproximation(
          NumericalApproximation::CubicHermite);

      vector<double> xvalues(p_cache.size());
      vector<double> xhermiteYValues(p_cache.size());
      vector<double> yhermiteYValues(p_cache.size());
      vector<double> zhermiteYValues(p_cache.size());

      vector<double> xhermiteVelocities(p_cache.size());
      vector<double> yhermiteVelocities(p_cache.size());
      vector<double> zhermiteVelocities(p_cache.size());

      for(unsigned int i = 0; i < p_cache.size(); i++) {
        vector<double> &cache = p_cache[i];
        double &cacheTime = p_cacheTime[i];
        xvalues[i] = (cacheTime - p_baseTime) / p_timeScale;

        xhermiteYValues[i] = cache[0];
        yhermiteYValues[i] = cache[1];
        zhermiteYValues[i] = cache[2];

        if(p_hasVelocity) {  // Line scan camera
          vector<double> &cacheVelocity = p_cacheVelocity[i];
          xhermiteVelocities[i] = cacheVelocity[0];
          yhermiteVelocities[i] = cacheVelocity[1];
          zhermiteVelocities[i] = cacheVelocity[2];
        }
        else { // Not line scan camera
          throw IException(IException::Io, "No velocities available.",
                           _FILEINFO_);
        }
      }

      p_xhermite->AddData(xvalues, xhermiteYValues);
      p_xhermite->AddCubicHermiteDeriv(xhermiteVelocities);

      p_yhermite->AddData(xvalues, yhermiteYValues);
      p_yhermite->AddCubicHermiteDeriv(yhermiteVelocities);

      p_zhermite->AddData(xvalues, zhermiteYValues);
      p_zhermite->AddCubicHermiteDeriv(zhermiteVelocities);
    }

    // Next line added 07/13/2009 to prevent Camera unit test from bombing
    // because time is outside domain. DAC Also added etype to Evaluate calls
    NumericalApproximation::ExtrapType etype =
        NumericalApproximation::Extrapolate;

    vector<double> &coordinate = p_coordinate;
    double sTime = (p_et - p_baseTime) / p_timeScale;
    coordinate[0] = p_xhermite->Evaluate(sTime, etype);
    coordinate[1] = p_yhermite->Evaluate(sTime, etype);
    coordinate[2] = p_zhermite->Evaluate(sTime, etype);

    vector<double> &velocity = p_velocity;
    velocity[0] = p_xhermite->EvaluateCubicHermiteFirstDeriv(sTime);
    velocity[1] = p_yhermite->EvaluateCubicHermiteFirstDeriv(sTime);
    velocity[2] = p_zhermite->EvaluateCubicHermiteFirstDeriv(sTime);
  }



  /**
   * This is a protected method that is called by
   * SetEphemerisTime() when Source type is PolyFunction.  It
   * calculates J2000 coordinates (x,y,z) of the body that
   * correspond to a given et in seconds. These coordinates are
   * obtained by using an nth degree polynomial function fit to
   * each coordinate of the position vector.
   * @see SetEphemerisTime()
   * @internal
   *   @history 2011-01-05 Debbie A. Cook - Original version
   */
  void Position::SetEphemerisTimePolyFunction() {
    // Create the empty functions
    Isis::PolynomialUnivariate functionX(p_degree);
    Isis::PolynomialUnivariate functionY(p_degree);
    Isis::PolynomialUnivariate functionZ(p_degree);

    // Load the coefficients to define the functions
    functionX.SetCoefficients(p_coefficients[0]);
    functionY.SetCoefficients(p_coefficients[1]);
    functionZ.SetCoefficients(p_coefficients[2]);

    // Normalize the time
    double rtime;
    rtime = (p_et - p_baseTime) / p_timeScale;

    // Evaluate the polynomials at current et to get position;
    p_coordinate[0] = functionX.Evaluate(rtime);
    p_coordinate[1] = functionY.Evaluate(rtime);
    p_coordinate[2] = functionZ.Evaluate(rtime);

    if(p_hasVelocity) {

      if( p_degree == 0) {
        p_velocity = p_cacheVelocity[0];
      }
      else {
        p_velocity[0] = ComputeVelocityInTime(WRT_X);
        p_velocity[1] = ComputeVelocityInTime(WRT_Y);
        p_velocity[2] = ComputeVelocityInTime(WRT_Z);
      }

//         p_velocity[0] = functionX.DerivativeVar(rtime);
//         p_velocity[1] = functionY.DerivativeVar(rtime);
//         p_velocity[2] = functionZ.DerivativeVar(rtime);
    }
  }


  /**
   * This is a protected method that is called by
   * SetEphemerisTime() when Source type is PolyFunctionOverHermiteConstant.  It
   * calculates J2000 coordinates (x,y,z) of the body that correspond to a given
   * et in seconds. These coordinates are obtained by adding a constant cubic
   * Hermite spline added to an nth degree polynomial function fit to
   * each coordinate of the position vector.
   * @see SetEphemerisTime()
   * @internal
   *   @history 2012-01-25 Debbie A. Cook - Original version
   */
  void Position::SetEphemerisTimePolyFunctionOverHermiteConstant() {
    SetEphemerisTimeHermiteCache();
    std::vector<double> hermiteCoordinate = p_coordinate;

//    std::cout << hermiteCoordinate << std::endl;

    std::vector<double> hermiteVelocity = p_velocity;
    SetEphemerisTimePolyFunction();

    for (int index = 0; index < 3; index++) {
      p_coordinate[index] += hermiteCoordinate[index];
      p_velocity[index] += hermiteVelocity[index];
    }
  }


  /**
   * This is a protected method that is called by
   * SetEphemerisTime() when Source type is Spice.  It
   * calculates J2000 coordinates (x,y,z) of the body that
   * correspond to a given et in seconds.  The coordinates are
   * obtained from a valid NAIF spk kernel.  The SPK kernel must
   * contain positions for the body code specified in the
   * constructor at the given time and it must be loaded using the
   * SpiceKernel class.
   * @see SetEphemerisTime()
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Original version
   *            (moved code from SetEphemerisTime() to its own
   *            method)
   */
  void Position::SetEphemerisTimeSpice() {

    double state[6];
    bool hasVelocity;
    double lt;

    // NOTE:  Prefer method getter access as some are virtualized and portray
    // the appropriate internal representation!!!
    computeStateVector(getAdjustedEphemerisTime(), getTargetCode(), getObserverCode(),
                       "J2000", GetAberrationCorrection(), state, hasVelocity,
                       lt);

    // Set the internal state
    setStateVector(state, hasVelocity);
    setLightTime(lt);
    return;
  }


  /**
   * This method reduces the cache for position, time and velocity
   * to the minimum number of values needed to interpolate the
   * J2000 coordinates using a Hermite spline, given a tolerance
   * of deviation from the NAIF values.
   *
   * @param tolerance Maximum error allowed between NAIF kernel
   *                  coordinate values and values interpolated by
   *                  the Hermite spline.
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Original version.
   */
  void Position::Memcache2HermiteCache(double tolerance) {
    if(p_source == HermiteCache) {
      return;
    }
    else if(p_source != Memcache) {
      throw IException(IException::Programmer,
                       "Source type is not Memcache, cannot convert.",
                       _FILEINFO_);
    }

    // make sure base time is set before it is needed
    p_overrideTimeScale = 1.;
    p_override = ScaleOnly;
    ComputeBaseTime();

    // find current size of cache
    int n = p_cacheTime.size() - 1;

    // create 3 starting values for the new table
    vector <int> inputIndices;
    inputIndices.push_back(0);
    inputIndices.push_back(n / 2);
    inputIndices.push_back(n);

    // find all indices needed to make a hermite table within the appropriate tolerance
    vector <int> indexList = HermiteIndices(tolerance, inputIndices);

    // remove all lines from cache vectors that are not in the index list????
    for(int i = n; i >= 0; i--) {
      if(!binary_search(indexList.begin(), indexList.end(), i)) {
        p_cache.erase(p_cache.begin() + i);
        p_cacheTime.erase(p_cacheTime.begin() + i);
        p_cacheVelocity.erase(p_cacheVelocity.begin() + i);
      }
    }

    p_source = HermiteCache;
  }

  /**
   * This method is called by Memcache2HermiteCache() to determine
   * which indices from the orginal cache should be saved in the
   * reduced cache. It is a recursive method that starts with an
   * index list of 3 elements (first, center and last index
   * values) and adds values to this list if the tolerance is not
   * met.
   *
   * @param tolerance Maximum error allowed between NAIF kernel
   *                  coordinate values and values interpolated by
   *                  the Hermite spline.
   * @param indexList Vector containing the list of indices to be
   *                  kept in the cache.  This list grows as the
   *                  method is recursively called
   * @return <b>vector/<double/></b> Vector containing final list
   *         of indices that will be kept for the Hermite cache.
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Original version.
   *   @history 2009-08-14 Debbie A. Cook - Corrected indexing
   *            error in loop.
   */
  std::vector<int> Position::HermiteIndices(double tolerance, std::vector <int> indexList) {

    unsigned int n = indexList.size();
    double sTime;

    NumericalApproximation xhermite(NumericalApproximation::CubicHermite);
    NumericalApproximation yhermite(NumericalApproximation::CubicHermite);
    NumericalApproximation zhermite(NumericalApproximation::CubicHermite);
    for(unsigned int i = 0; i < indexList.size(); i++) {
      sTime = (p_cacheTime[indexList[i]] - p_baseTime) / p_timeScale;
      xhermite.AddData(sTime, p_cache[indexList[i]][0]);  // add time, x-position to x spline
      yhermite.AddData(sTime, p_cache[indexList[i]][1]);  // add time, y-position to y spline
      zhermite.AddData(sTime, p_cache[indexList[i]][2]);  // add time, z-position to z spline

      if(p_hasVelocity) {  // Line scan camera
        xhermite.AddCubicHermiteDeriv(p_cacheVelocity[i][0]); // add x-velocity to x spline
        yhermite.AddCubicHermiteDeriv(p_cacheVelocity[i][1]); // add y-velocity to y spline
        zhermite.AddCubicHermiteDeriv(p_cacheVelocity[i][2]); // add z-velocity to z spline
      }
      else { // Not line scan camera
        throw IException(IException::Io, "No velocities available.", _FILEINFO_);
        // xhermite.AddCubicHermiteDeriv(0.0); // spacecraft didn't move => velocity = 0
        // yhermite.AddCubicHermiteDeriv(0.0); // spacecraft didn't move => velocity = 0
        // zhermite.AddCubicHermiteDeriv(0.0); // spacecraft didn't move => velocity = 0
      }
    }

    // loop through the saved indices from the end
    for(unsigned int i = indexList.size() - 1; i > 0; i--) {
      double xerror = 0;
      double yerror = 0;
      double zerror = 0;

      // check every value of the original kernel values within interval
      for(int line = indexList[i-1] + 1; line < indexList[i]; line++) {
        sTime = (p_cacheTime[line] - p_baseTime) / p_timeScale;

        // find the errors at each value
        xerror = fabs(xhermite.Evaluate(sTime) - p_cache[line][0]);
        yerror = fabs(yhermite.Evaluate(sTime) - p_cache[line][1]);
        zerror = fabs(zhermite.Evaluate(sTime) - p_cache[line][2]);

        if(xerror > tolerance || yerror > tolerance || zerror > tolerance) {
          // if any error is greater than tolerance, no need to continue looking, break
          break;
        }
      }

      if(xerror < tolerance && yerror < tolerance && zerror < tolerance) {
        // if errors are less than tolerance after looping interval, no new point is necessary
        continue;
      }
      else {
        // if any error is greater than tolerance, add midpoint of interval to indexList vector
        indexList.push_back((indexList[i] + indexList[i-1]) / 2);
      }
    }

    if(indexList.size() > n) {
      sort(indexList.begin(), indexList.end());
      indexList = HermiteIndices(tolerance, indexList);
    }
    return indexList;
  }


  /**
   * Removes the entire cache from memory.
   */
  void Position::ClearCache() {
    p_cache.clear();
    p_cacheVelocity.clear();
    p_cacheTime.clear();

    if(p_xhermite) {
      delete p_xhermite;
      p_xhermite = NULL;
    }

    if(p_yhermite) {
      delete p_xhermite;
      p_xhermite = NULL;
    }

    if(p_zhermite) {
      delete p_xhermite;
      p_xhermite = NULL;
    }
  }



  /** Set the degree of the polynomials to be fit to the
   * three position coordinates for the time period covered by the
   * cache, coordinate = c0 + c1*t + c2*t**2 + ... + cn*t**n,
   * where t = (time - p_baseTime) / p_timeScale, and n = p_degree.
   *
   * @param [in] degree Degree of the polynomial to be fit
   *
   */
  void Position::SetPolynomialDegree(int degree) {
    // Adjust the degree for the data
    if(p_fullCacheSize == 1) {
      degree = 0;
    }
    else if(p_fullCacheSize == 2) {
      degree = 1;
    }
    // If polynomials have not been applied yet simply set the degree and return
    if(!p_degreeApplied) {
      p_degree = degree;
    }

    // Otherwise the existing polynomials need to be either expanded ...
    else if(p_degree < degree) {   // (increase the number of terms)
      std::vector<double> coefX(p_coefficients[0]),
          coefY(p_coefficients[1]),
          coefZ(p_coefficients[2]);

      for(int icoef = p_degree + 1;  icoef <= degree; icoef++) {
        coefX.push_back(0.);
        coefY.push_back(0.);
        coefZ.push_back(0.);
      }
      p_degree = degree;
      SetPolynomial(coefX, coefY, coefZ);
    }
    // ... or reduced (decrease the number of terms)
    else if(p_degree > degree) {
      std::vector<double> coefX(degree + 1),
          coefY(degree + 1),
          coefZ(degree + 1);

      for(int icoef = 0;  icoef <= degree;  icoef++) {
        coefX.push_back(p_coefficients[0][icoef]);
        coefY.push_back(p_coefficients[1][icoef]);
        coefZ.push_back(p_coefficients[2][icoef]);
      }
      p_degree = degree;
      SetPolynomial(coefX, coefY, coefZ);
    }
  }



  /** Cache J2000 position over existing cached time range using
   *  table
   *
   * This method will reload an internal cache with positions
   * formed from coordinates in a table
   *
   * @param table  An ISIS table blob containing valid J2000
   *               coordinate/time values.
   * @internal
   *   @history 2009-08-03 Jeannie Walldren - Original version.
   */
  void Position::ReloadCache(Table &table) {
    p_source = Spice;
    ClearCache();
    LoadCache(table);
  }

  /** Load the time cache.  This method should works with the LoadCache(startTime, endTime, size) method
   *  to load the time cache.
   *
   */
  void Position::LoadTimeCache() {
    // Loop and load the time cache
    double cacheSlope = 0.0;

    if(p_fullCacheSize > 1)
      cacheSlope = (p_fullCacheEndTime - p_fullCacheStartTime) /
         (double)(p_fullCacheSize - 1);

    for(int i = 0; i < p_fullCacheSize; i++) {
      double et = p_fullCacheStartTime + (double) i * cacheSlope;
      p_cacheTime.push_back(et);
    }
  }

  std::vector<double> Position::LoadTimeCache(int startTime, int endTime, int size) {
    // Loop and load the time cache
    double cacheSlope = 0.0;
    std::vector<double> cacheTime;

    if(size > 1)
      cacheSlope = (endTime - startTime) / (double)(size - 1);

    for(int i = 0; i < size; i++) {
      double et = startTime + (double) i * cacheSlope;
      cacheTime.push_back(et);
    }

    return cacheTime;
  }

  /** Compute and return the coordinate at the center time
   *
   */
  const std::vector<double> &Position::GetCenterCoordinate() {
    // Compute the center time
    double etCenter = (p_fullCacheEndTime + p_fullCacheStartTime) / 2.;
    SetEphemerisTime(etCenter);

    return Coordinate();
  }


  /** Compute the velocity with respect to time instead of
   *  scaled time.
   *
   * @param coef  A Position polynomial function partial type
   *
   * @internal
   *   @history 2011-02-12 Debbie A. Cook - Original version.
   */
  double Position::ComputeVelocityInTime(PartialType var) {
    double velocity = 0.;

    for (int icoef=1; icoef <= p_degree; icoef++) {
      velocity += icoef*p_coefficients[var][icoef]*pow((p_et - p_baseTime), (icoef - 1))
                  / pow(p_timeScale, icoef);
    }

    return velocity;
  }


  /** Extrapolate position for a given time assuming a constant velocity.
   *  The position and velocity at the current time will be used to
   *  extrapolate position at the input time.  If velocity does not
   *  exist, the value at the current time will be output.  The caller
   *  must make sure to call SetEphemerisTime to set the time to the
   *  time to be used for the extrapolation.
   *
   * @param [in]   timeEt    The time of the position to be extrapolated
   * @param [out]            An extrapolated position at the input time
   *
   */
   std::vector<double> Position::Extrapolate(double timeEt) {
     if (!p_hasVelocity) return p_coordinate;

     double diffTime = timeEt - p_et;
     std::vector<double> extrapPos(3, 0.);
     extrapPos[0] = p_coordinate[0] + diffTime*p_velocity[0];
     extrapPos[1] = p_coordinate[1] + diffTime*p_velocity[1];
     extrapPos[2] = p_coordinate[2] + diffTime*p_velocity[2];

     return extrapPos;
   }


  /**
   * This method returns the Hermite coordinate for the current time for
   * PolyFunctionOverHermiteConstant functions.
   * @see SetEphemerisTime()
   * @internal
   *   @history 2012-02-05 Debbie A. Cook - Original version
   */
  std::vector<double> Position::HermiteCoordinate() {
    if (p_source != PolyFunctionOverHermiteConstant) {
      throw IException(IException::Programmer,
        "Hermite coordinates only available for PolyFunctionOverHermiteConstant",
          _FILEINFO_);
    }

    // Save the current coordinate so it can be reset
    std::vector<double> coordinate = p_coordinate;
    SetEphemerisTimeHermiteCache();
    std::vector<double> hermiteCoordinate = p_coordinate;
    p_coordinate = coordinate;
    return hermiteCoordinate;
  }

  //=========================================================================
  //  Observer/Target swap and light time correction methods
  //=========================================================================

/**
 * @brief Returns observer code
 *
 * This methods returns the proper observer code as specified in constructor.
 * Code has been subjected to swapping if requested.
 *
 * @author 2012-10-29 Kris Becker
 *
 * @return int NAIF code of observer
 */
  int Position::getObserverCode() const {
    return (p_observerCode);
  }

/**
 * @brief Returns target code
 *
 * This methods returns the proper target code as specified in constructor.
 * Code has been subjected to swapping if requested.
 *
 * @author 2012-10-29 Kris Becker
 *
 * @return int NAIF code for target body
 */
  int Position::getTargetCode() const {
    return (p_targetCode);
  }


/**
 * @brief Returns adjusted ephemeris time
 *
 * This method returns the actual ephemeris time adjusted by a specifed time
 * bias.  The bias typically comes explicitly from the camera model but could
 * be adjusted by the environment they are utlized in.
 *
 * @author 2012-10-29 Kris Becker
 *
 * @return double Adjusted ephemeris time with time bias applied
 */
  double Position::getAdjustedEphemerisTime() const {
    return (EphemerisTime() + GetTimeBias());
  }


/**
 * @brief Computes the state vector of the target w.r.t observer
 *
 * This method computes the state vector of the target that is relative of the
 * observer.  It first attempts to retrieve with velocity vectors. If that
 * fails, it makes an additional attempt to get the state w/o velocity vectors.
 * The final result is indicated by the hasVelocity parameter.
 *
 * The parameters to this routine are the same as the NAIF spkez_c/spkezp_c
 * routines. See
 * http://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/cspice/spkez_c.html for
 * complete description.
 *
 * Note that this routine does not actually affect the internals of this object
 * (hence the constness of the method).  It is up to the caller to apply the
 * results and potentially handle swapping of observer/target and light time
 * correction. See SpacecraftPosition for additional details on this
 * application of the results.
 *
 * @author 2012-10-29 Kris Becker
 *
 * @param et          Time to compute state vector for
 * @param target      NAIF target code
 * @param observer    NAIF observer code
 * @param refFrame    Reference frame to express coordinates in (e.g., J2000)
 * @param abcorr      Stellar aberration correction option
 * @param state       Returns the 6-element state vector in target body fixed
 *                    coordinates
 * @param hasVelocity Returns knowledge of whether the velocity vector is valid
 * @param lightTime   Returns the light time correct resulting from the request
 *                    if applicable
 */
  void Position::computeStateVector(double et, int target, int observer,
                                         const QString &refFrame,
                                         const QString &abcorr,
                                         double state[6], bool &hasVelocity,
                                         double &lightTime) const {

    // First try getting the entire state (including the velocity vector)
    NaifStatus::CheckErrors();
    hasVelocity = true;
    lightTime = 0.0;
    spkez_c((SpiceInt) target, (SpiceDouble) et, refFrame.toLatin1().data(),
            abcorr.toLatin1().data(), (SpiceInt) observer, state, &lightTime);

    // If Naif fails attempting to get the entire state, assume the velocity vector is
    // not available and just get the position.  First turn off Naif error reporting and
    // return any error without printing them.
    SpiceBoolean spfailure = failed_c();
    reset_c();                   // Reset Naif error system to allow caller to recover
    if ( spfailure ) {
      hasVelocity = false;
      lightTime = 0.0;
      spkezp_c((SpiceInt) target, (SpiceDouble) et, refFrame.toLatin1().data(),
               abcorr.toLatin1().data(), (SpiceInt) observer, state, &lightTime);
    }
    NaifStatus::CheckErrors();
    return;
  }

/**
 * @brief Sets the state of target relative to observer
 *
 * This method sets the state of the target (vector) relative to the observer.
 * Note that is the only place where the swap of observer/target adjustment to
 * the state vector is handled.  All contributors to this computation *must*
 * compute the state vector representing the position and velocity of the
 * target relative to the observer where the first three components of state[]
 * are the x-, y- and z-component cartesian coordinates of the target's
 * position; the last three are the corresponding velocity vector.
 *
 * This routine maintains the directional integrity of the state vector should
 * the observer/target be swapped.  See the documentation for the spkez_c at
 * http://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/cspice/spkez_c.html.
 *
 * @author 2012-10-29 Kris Becker
 *
 * @param state        State vector.  First three components of this 6 element
 *                     array is the body fixed coordinates of the vector from
 *                     the target to the observer.  The last three components
 *                     are the velocity state.
 * @param hasVelocity  If true, then the velocity components of the state
 *                     vector are valid, otherwise they should be ignored.
 */
  void Position::setStateVector(const double state[6],
                                     const bool &hasVelocity) {

    p_coordinate[0] = state[0];
    p_coordinate[1] = state[1];
    p_coordinate[2] = state[2];

    if ( hasVelocity ) {
      p_velocity[0] = state[3];
      p_velocity[1] = state[4];
      p_velocity[2] = state[5];
    }
    else {
      p_velocity[0] = 0.0;
      p_velocity[1] = 0.0;
      p_velocity[2] = 0.0;
    }
    p_hasVelocity = hasVelocity;

    // Negate vectors if swap of observer target is requested so interface
    // remains consistent
    if (m_swapObserverTarget) {
      vminus_c(&p_velocity[0],   &p_velocity[0]);
      vminus_c(&p_coordinate[0], &p_coordinate[0]);
    }

    return;
  }

  /** Inheritors can set the light time if indicated */
  void Position::setLightTime(const double &lightTime) {
    m_lt = lightTime;
    return;
  }

  // /** Resets the source interpolation of the position.
  //  *
  //  * @param [in]   source    The interpolation to use for calculating position
  //  *
  //  */
  //  void SetSource(Source source) {
  //    p_source = source;
  //    return;
  //  }
}
