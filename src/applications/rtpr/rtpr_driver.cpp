/*
 *     Copyright (c) 2013 Battelle Memorial Institute
 *     Licensed under modified BSD License. A copy of this license can be found
 *     in the LICENSE file in the top level directory of this distribution.
 */
// -------------------------------------------------------------
/**
 * @file   rtpr_driver.cpp
 * @author Bruce Palmer
 * @date   2019-10-09 13:12:46 d3g293
 *
 * @brief Driver for real-time path rating calculation that make use of the
 *        powerflow module to implement individual power flow simulations for
 *        each contingency. The different contingencies are distributed across
 *        separate communicators using the task manager.
 *
 *
 */
// -------------------------------------------------------------

#include "gridpack/include/gridpack.hpp"
#include "gridpack/applications/modules/powerflow/pf_app_module.hpp"
#include "rtpr_driver.hpp"

//#define USE_SUCCESS
// Sets up multiple communicators so that individual contingency calculations
// can be run concurrently

/**
 * Basic constructor
 */
gridpack::rtpr::RTPRDriver::RTPRDriver(void)
{
}

/**
 * Basic destructor
 */
gridpack::rtpr::RTPRDriver::~RTPRDriver(void)
{
}

/**
 * Get list of contingencies from external file
 * @param cursor pointer to contingencies in input deck
 * @return vector of contingencies
 */
std::vector<gridpack::powerflow::Contingency>
  gridpack::rtpr::RTPRDriver::getContingencies(
      gridpack::utility::Configuration::ChildCursors &contingencies)
{
  // The contingencies ChildCursors argument is a vector of configuration
  // pointers. Each element in the vector is pointing at a seperate Contingency
  // block within the Contingencies block in the input file.
  std::vector<gridpack::powerflow::Contingency> ret;
  int size = contingencies.size();
  int i, idx;
  // Create string utilities object to help parse file
  gridpack::utility::StringUtils utils;
  // Loop over all child cursors
  for (idx = 0; idx < size; idx++) {
    std::string ca_type;
    contingencies[idx]->get("contingencyType",&ca_type);
    // Contingency name is used to direct output to different files for each
    // contingency
    std::string ca_name;
    contingencies[idx]->get("contingencyName",&ca_name);
    if (ca_type == "Line") {
      std::string buses;
      contingencies[idx]->get("contingencyLineBuses",&buses);
      std::string names;
      contingencies[idx]->get("contingencyLineNames",&names);
      // Tokenize bus string to get a list of individual buses
      std::vector<std::string> string_vec = utils.blankTokenizer(buses);
      // Convert buses from character strings to ints
      std::vector<int> bus_ids;
      for (i=0; i<string_vec.size(); i++) {
        bus_ids.push_back(atoi(string_vec[i].c_str()));
      }
      string_vec.clear();
      // Tokenize names string to get a list of individual line tags
      string_vec = utils.blankTokenizer(names);
      std::vector<std::string> line_names;
      // clean up line tags so that they are exactly two characters
      for (i=0; i<string_vec.size(); i++) {
        line_names.push_back(utils.clean2Char(string_vec[i]));
      }
      // Check to make sure we found everything
      if (bus_ids.size() == 2*line_names.size()) {
        // Add contingency parameters to contingency struct
        gridpack::powerflow::Contingency contingency;
        contingency.p_name = ca_name;
        contingency.p_type = Branch;
        int i;
        for (i = 0; i < line_names.size(); i++) {
          contingency.p_from.push_back(bus_ids[2*i]);
          contingency.p_to.push_back(bus_ids[2*i+1]);
          contingency.p_ckt.push_back(line_names[i]);
          contingency.p_saveLineStatus.push_back(true);
        }
        // Add branch contingency to contingency list
        ret.push_back(contingency);
      }
    } else if (ca_type == "Generator") {
      std::string buses;
      contingencies[idx]->get("contingencyBuses",&buses);
      std::string gens;
      contingencies[idx]->get("contingencyGenerators",&gens);
      // Tokenize bus string to get a list of individual buses
      std::vector<std::string> string_vec = utils.blankTokenizer(buses);
      std::vector<int> bus_ids;
      // Convert buses from character strings to ints
      for (i=0; i<string_vec.size(); i++) {
        bus_ids.push_back(atoi(string_vec[i].c_str()));
      }
      string_vec.clear();
      // Tokenize gens string to get a list of individual generator tags
      string_vec = utils.blankTokenizer(gens);
      std::vector<std::string> gen_ids;
      // clean up generator tags so that they are exactly two characters
      for (i=0; i<string_vec.size(); i++) {
        gen_ids.push_back(utils.clean2Char(string_vec[i]));
      }
      // Check to make sure we found everything
      if (bus_ids.size() == gen_ids.size()) {
        gridpack::powerflow::Contingency contingency;
        contingency.p_name = ca_name;
        contingency.p_type = Generator;
        int i;
        for (i = 0; i < bus_ids.size(); i++) {
          contingency.p_busid.push_back(bus_ids[i]);
          contingency.p_genid.push_back(gen_ids[i]);
          contingency.p_saveGenStatus.push_back(true);
        }
        // Add generator contingency to contingency list
        ret.push_back(contingency);
      }
    }
  }
  return ret;
}

/**
 * Dummy struct to get around some problems with vectors of characters
 */
struct char2 {
  char str[3];
};

/**
 * Create a list of all N-1 generator contingencies for a given area and
 * zone. If zone less than 1, then just use the entire area.
 * @param network power grid network on which contingencies are defined 
 * @param area index of area that will generate contingencies
 * @param zone index of zone that will generate contingencies
 * @return vector of contingencies
 */
std::vector<gridpack::powerflow::Contingency> 
  gridpack::rtpr::RTPRDriver::createGeneratorContingencies(
  boost::shared_ptr<gridpack::powerflow::PFNetwork> network, int area,
  int zone)
{
  std::vector<gridpack::powerflow::Contingency> ret;
  gridpack::utility::StringUtils util;
  int nbus = network->numBuses();
  int i,j,iarea, izone;
  std::vector<int> bus_ids;
  std::vector<char2> tags;
  char2 buf;
  int nproc = network->communicator().size();
  int me = network->communicator().rank();
  // Loop over all buses
  for (i=0; i<nbus; i++) {
    if (network->getActiveBus(i)) {
      // Get data collection object
      gridpack::component::DataCollection *data
        = network->getBusData(i).get();
      int ngen=0; 
      data->getValue(BUS_AREA, &iarea);
      if (zone > 0) {
        data->getValue(BUS_ZONE, &izone);
      } else {
        izone = zone;
      }
      if (iarea == area && izone == zone) {
        if (data->getValue(GENERATOR_NUMBER, &ngen)) {
          for (j=0; j<ngen; j++) {
            // Check generator status
            int status = 0;
            data->getValue(GENERATOR_STAT, &status, j);
            if (status != 0) {
              // Generator is active in base case. Add a contingency
              bus_ids.push_back(network->getOriginalBusIndex(i));
              std::string tag, clean_tag;
              data->getValue(GENERATOR_ID,&tag,j);
              clean_tag = util.clean2Char(tag);
              strncpy(buf.str,clean_tag.c_str(),2);
              buf.str[2] = '\0';
              tags.push_back(buf);
            }
          }
        }
      }
    }
  }
  // Have a list of local faults. Find out total number of faults.
  int nflt = bus_ids.size();
  std::vector<int> sizes(nproc);
  for (i=0; i<nproc; i++) sizes[i] = 0;
  sizes[me] = nflt;
  network->communicator().sum(&sizes[0], nproc);
  // create global list containing buses and generator tags
  nflt = 0;
  for (i=0; i<nproc; i++) nflt += sizes[i];
  if (nflt == 0) {
    return ret;
  }
  int g_bus = GA_Create_handle();
  int one = 1;
  NGA_Set_data(g_bus,one,&nflt,C_INT);
  NGA_Allocate(g_bus);
  int g_ids = GA_Create_handle();
  int idType = NGA_Register_type(sizeof(char2));
  NGA_Set_data(g_ids,1,&nflt,idType);
  NGA_Allocate(g_ids);
  int lo, hi;
  lo = 0; 
  for (i=0; i<me; i++) lo += sizes[i];
  hi = lo + sizes[me] - 1;
  // Create a complete list on all processes
  if (lo <= hi) NGA_Put(g_bus,&lo,&hi,&bus_ids[0],&one);
  if (lo <= hi) NGA_Put(g_ids,&lo,&hi,&tags[0],&one);
  network->communicator().sync();
  bus_ids.resize(nflt);
  tags.resize(nflt);
  lo = 0;
  hi = nflt-1;
  NGA_Get(g_bus,&lo,&hi,&bus_ids[0],&one);
  NGA_Get(g_ids,&lo,&hi,&tags[0],&one);
  GA_Destroy(g_bus);
  GA_Destroy(g_ids);
  NGA_Deregister_type(idType);
  // create a list of contingencies
  char sbuf[128];
  for (i=0; i<nflt; i++) {
    gridpack::powerflow::Contingency fault;
    fault.p_type = Generator;
    fault.p_busid.push_back(bus_ids[i]);
    std::string tag = tags[i].str;
    fault.p_genid.push_back(util.clean2Char(tag));
    fault.p_saveGenStatus.push_back(true);
    sprintf(sbuf,"GenCTG%d",i+1);
    fault.p_name = sbuf;
    ret.push_back(fault);
  }
  return ret;
}

/**
 * Create a list of all N-1 branch contingencies for a given area and
 * zone. If zone less than 1, then just use the entire area.
 * @param network power grid network on which contingencies are defined 
 * @param area index of area that will generate contingencies
 * @param zone index of zone that will generate contingencies
 * @return vector of contingencies
 */
std::vector<gridpack::powerflow::Contingency>
  gridpack::rtpr::RTPRDriver::createBranchContingencies(
  boost::shared_ptr<gridpack::powerflow::PFNetwork> network, int area,
  int zone)
{
  std::vector<gridpack::powerflow::Contingency> ret;
  gridpack::utility::StringUtils util;
  int nbranch = network->numBranches();
  int i,j,idx1,idx2,i1,i2,area1,area2,zone1,zone2;
  std::vector<int> bus_from;
  std::vector<int> bus_to;
  std::vector<char2> tags;
  char2 buf;
  // Loop over all branches
  for (i=0; i<nbranch; i++) {
    if (network->getActiveBranch(i)) {
      // Get data collection object
      gridpack::component::DataCollection *data
        = network->getBranchData(i).get();
      int nline=0; 
      network->getBranchEndpoints(i,&i1,&i2);
      network->getBusData(i1)->getValue(BUS_AREA,&area1);
      network->getBusData(i2)->getValue(BUS_AREA,&area2);
      if (zone > 0) {
        network->getBusData(i1)->getValue(BUS_ZONE,&zone1);
        network->getBusData(i2)->getValue(BUS_ZONE,&zone2);
      } else {
        zone1 = zone;
        zone2 = zone;
      }
      if (area == area1 && area == area2 && zone == zone1 && zone == zone2) {
        if (data->getValue(BRANCH_NUM_ELEMENTS, &nline)) {
          for (j=0; j<nline; j++) {
            // Check generator status
            int status = 0;
            data->getValue(BRANCH_STATUS, &status, j);
            if (status != 0) {
              // Generator is active in base case. Add a contingency
              network->getOriginalBranchEndpoints(i,&idx1,&idx2);
              bus_from.push_back(idx1);
              bus_to.push_back(idx2);
              std::string tag, clean_tag;
              data->getValue(BRANCH_CKT,&tag,j);
              clean_tag = util.clean2Char(tag);
              strncpy(buf.str,clean_tag.c_str(),2);
              buf.str[2] = '\0';
              tags.push_back(buf);
            }
          }
        }
      }
    }
  }
  // Have a list of local faults. Find out total number of faults.
  int nflt = bus_from.size();
  int nproc = network->communicator().size();
  int me = network->communicator().rank();
  std::vector<int> sizes(nproc);
  for (i=0; i<nproc; i++) sizes[i] = 0;
  sizes[me] = nflt;
  network->communicator().sum(&sizes[0], nproc);
  // create global list containing buses and generator tags
  nflt = 0;
  for (i=0; i<nproc; i++) nflt += sizes[i];
  if (nflt == 0) {
    return ret;
  }
  int g_from = GA_Create_handle();
  int one = 1;
  NGA_Set_data(g_from,one,&nflt,C_INT);
  NGA_Allocate(g_from);
  int g_to = GA_Create_handle();
  NGA_Set_data(g_to,one,&nflt,C_INT);
  NGA_Allocate(g_to);
  int g_ids = GA_Create_handle();
  int idType = NGA_Register_type(sizeof(char2));
  NGA_Set_data(g_ids,1,&nflt,idType);
  NGA_Allocate(g_ids);
  int lo, hi;
  lo = 0; 
  for (i=0; i<me; i++) lo += sizes[i];
  hi = lo + sizes[me] - 1;
  // Create a complete list on all processes
  if (lo <= hi) NGA_Put(g_from,&lo,&hi,&bus_from[0],&one);
  if (lo <= hi) NGA_Put(g_to,&lo,&hi,&bus_to[0],&one);
  if (lo <= hi) NGA_Put(g_ids,&lo,&hi,&tags[0],&one);
  network->communicator().sync();
  bus_from.resize(nflt);
  bus_to.resize(nflt);
  tags.resize(nflt);
  lo = 0;
  hi = nflt-1;
  NGA_Get(g_from,&lo,&hi,&bus_from[0],&one);
  NGA_Get(g_to,&lo,&hi,&bus_to[0],&one);
  NGA_Get(g_ids,&lo,&hi,&tags[0],&one);
  GA_Destroy(g_from);
  GA_Destroy(g_to);
  GA_Destroy(g_ids);
  NGA_Deregister_type(idType);
  // create a list of contingencies
  char sbuf[128];
  for (i=0; i<nflt; i++) {
    gridpack::powerflow::Contingency fault;
    fault.p_type = Branch;
    fault.p_from.push_back(bus_from[i]);
    fault.p_to.push_back(bus_to[i]);
    std::string tag = tags[i].str;
    fault.p_ckt.push_back(util.clean2Char(tag));
    fault.p_saveLineStatus.push_back(true);
    sprintf(sbuf,"LineCTG%d",i+1);
    fault.p_name = sbuf;
    ret.push_back(fault);
  }
  return ret;
}

/**
 * Get list of tie lines
 * @param cursor pointer to tie lines in input deck
 * @return vector of contingencies
 */
std::vector<gridpack::rtpr::TieLine>
  gridpack::rtpr::RTPRDriver::getTieLines(
      gridpack::utility::Configuration::ChildCursors &tielines)
{
  // The ChildCursors argument is a vector of configuration
  // pointers. Each element in the vector is pointing at a seperate tieLine
  // block within the TieLines block in the input file.
  std::vector<gridpack::rtpr::TieLine> ret;
  int size = tielines.size();
  int i, idx;
  // Create string utilities object to help parse file
  gridpack::utility::StringUtils utils;
  // Loop over all child cursors
  for (idx = 0; idx < size; idx++) {
    std::string branchIDs;
    tielines[idx]->get("Branch",&branchIDs);
    std::vector<std::string> from_to = utils.blankTokenizer(branchIDs);
    gridpack::rtpr::TieLine tieline;
    tieline.from = atoi(from_to[0].c_str());
    tieline.to = atoi(from_to[1].c_str());
    std::string tag;
    tielines[idx]->get("Tag",&tag);
    std::string clean_tag = utils.clean2Char(tag);
    strncpy(tieline.tag,clean_tag.c_str(),2);
    tieline.tag[2] = '\0';
    ret.push_back(tieline);
  }
  return ret;
}

/**
 * Automatically create list of tie lines between area1,zone1 and
 * area2,zone2
 * @param area1 index of source area
 * @param zone1 index of source zone
 * @param area2 index of destination area
 * @param zone2 index of destination zone
 * @param tielines list of tie lines between area1,zone1 and area2,zone2
 */
std::vector<gridpack::rtpr::TieLine> 
  gridpack::rtpr::RTPRDriver::getTieLines(int area1, int zone1,
    int area2, int zone2)
{
  std::vector<gridpack::rtpr::TieLine> ret;
  // Loop through all branches and find the ones that connect area1,zone1 with
  // area2,zone2
  int nbranch = p_pf_network->numBranches();
  int i, j, idx1, idx2;
  int iarea1, iarea2, izone1, izone2, tzone1, tzone2;
  bool found;
  for (i=0; i<nbranch; i++) {
    if (p_pf_network->getActiveBranch(i)) {
      gridpack::powerflow::PFBranch *branch = p_pf_network->getBranch(i).get();
      p_pf_network->getBranchEndpoints(i,&idx1,&idx2);
      gridpack::powerflow::PFBus *bus1 = p_pf_network->getBus(idx1).get();
      gridpack::powerflow::PFBus *bus2 = p_pf_network->getBus(idx2).get();
      iarea1 = bus1->getArea();
      iarea2 = bus2->getArea();
      izone1 = bus1->getZone();
      izone2 = bus2->getZone();
      // Check bus1 corresponds to area1,zone1, bus 2 corresponds to area2/zone2
      found = false;
      if (zone1 > 0) {
        tzone1 = izone1;
      } else {
        tzone1 = zone1;
      }
      if (zone2 > 0) {
        tzone2 = izone2;
      } else {
        tzone2 = zone2;
      }
      if (iarea1 == area1 && tzone1 == zone1 &&
          iarea2 == area2 && tzone2 == zone2) {
        found = true;
        int from, to;
        p_pf_network->getOriginalBranchEndpoints(i,&from,&to);
        std::vector<std::string> tags = branch->getLineIDs();
        for (j=0; j<tags.size(); j++) {
          gridpack::rtpr::TieLine tieline;
          tieline.from = from;
          tieline.to = to;
          strncpy(tieline.tag,tags[j].c_str(),2);
          tieline.tag[2] = '\0';
          ret.push_back(tieline);
        }
      }
      // Check bus2 corresponds to area1,zone1, bus 1 corresponds to area2/zone2
      if (!found) {
        if (zone1 > 0) {
          tzone2 = izone1;
        } else {
          tzone2 = zone1;
        }
        if (zone2 > 0) {
          tzone1 = izone2;
        } else {
          tzone1 = zone2;
        }
        if (iarea2 == area1 && tzone1 == zone1 &&
            iarea1 == area2 && tzone2 == zone2) {
          int from, to;
          p_pf_network->getOriginalBranchEndpoints(i,&from,&to);
          std::vector<std::string> tags = branch->getLineIDs();
          for (j=0; j<tags.size(); j++) {
            gridpack::rtpr::TieLine tieline;
            tieline.from = from;
            tieline.to = to;
            strncpy(tieline.tag,tags[j].c_str(),2);
            tieline.tag[2] = '\0';
            ret.push_back(tieline);
          }
        }
      }
    }
  }
  // Have a list of local tie lines. Find out total number of tie lines.
  int ntie = ret.size();
  int nproc = p_pf_network->communicator().size();
  int me = p_pf_network->communicator().rank();
  std::vector<int> sizes(nproc);
  for (i=0; i<nproc; i++) sizes[i] = 0;
  sizes[me] = ntie;
  p_pf_network->communicator().sum(&sizes[0], nproc);
  // create global list of tie lines
  ntie = 0;
  for (i=0; i<nproc; i++) ntie += sizes[i];
  if (ntie == 0) {
    return ret;
  }
  int g_tie = GA_Create_handle();
  int one = 1;
  int itype = NGA_Register_type(sizeof(gridpack::rtpr::TieLine));
  NGA_Set_data(g_tie,one,&ntie,itype);
  NGA_Allocate(g_tie);
  int lo, hi;
  lo = 0; 
  for (i=0; i<me; i++) lo += sizes[i];
  hi = lo + sizes[me] - 1;
  // Create a complete list on all processes
  if (lo <= hi) NGA_Put(g_tie,&lo,&hi,&ret[0],&one);
  p_pf_network->communicator().sync();
  ret.resize(ntie);
  lo = 0;
  hi = ntie-1;
  NGA_Get(g_tie,&lo,&hi,&ret[0],&one);
  GA_Destroy(g_tie);
  NGA_Deregister_type(itype);
  return ret;
}

/**
 * Create a list of generators for a given area and zone. These
 * generators will be monitored to see if they exceed operating
 * specifications.
 * @param network power grid used for DS simulation
 * @param area index of area that will generate contingencies
 * @param zone index of zone that will generate contingencies
 * @param buses list of bus IDs that contain generators
 * @param tags list of generator IDs
 */
void  gridpack::rtpr::RTPRDriver::findWatchedGenerators(
  boost::shared_ptr<gridpack::dynamic_simulation::DSFullNetwork> network, int area,
  int zone, std::vector<int> &buses, std::vector<std::string> &tags)
{
  gridpack::utility::StringUtils util;
  int nbus = network->numBuses();
  int i,j,iarea, izone;
  std::vector<int> bus_ids;
  std::vector<char2> ctags;
  char2 buf;
  int nproc = network->communicator().size();
  int me = network->communicator().rank();
  // Loop over all buses
  for (i=0; i<nbus; i++) {
    if (network->getActiveBus(i)) {
      // Get data collection object
      gridpack::component::DataCollection *data
        = network->getBusData(i).get();
      int ngen=0; 
      data->getValue(BUS_AREA, &iarea);
      if (zone > 0) {
        data->getValue(BUS_ZONE, &izone);
      } else {
        izone = zone;
      }
      if (iarea == area && izone == zone) {
        if (data->getValue(GENERATOR_NUMBER, &ngen)) {
          for (j=0; j<ngen; j++) {
            // Check generator status
            int status = 0;
            data->getValue(GENERATOR_STAT, &status, j);
            if (status != 0) {
              // Generator is active in base case. Add a contingency
              bus_ids.push_back(network->getOriginalBusIndex(i));
              std::string tag, clean_tag;
              data->getValue(GENERATOR_ID,&tag,j);
              clean_tag = util.clean2Char(tag);
              strncpy(buf.str,clean_tag.c_str(),2);
              buf.str[2] = '\0';
              ctags.push_back(buf);
            }
          }
        }
      }
    }
  }

  // Have a list of local faults. Find out total number of faults.
  int ngen = bus_ids.size();
  std::vector<int> sizes(nproc);
  for (i=0; i<nproc; i++) sizes[i] = 0;
  sizes[me] = ngen;
  network->communicator().sum(&sizes[0], nproc);
  ngen = 0;
  for (i=0; i<nproc; i++) ngen += sizes[i];
  if (ngen == 0) {
    return;
  }
  // create a list of indices for local data values
  int offset = 0;
  for (i=0; i<me; i++) offset += sizes[i];
  std::vector<int> idx;
  for (i=0; i < bus_ids.size(); i++) idx.push_back(offset+i);
  // upload all values to a global vector
  gridpack::parallel::GlobalVector<int> busIDs(network->communicator());
  gridpack::parallel::GlobalVector<char2> genIDs(network->communicator());
  busIDs.addElements(idx,bus_ids);
  genIDs.addElements(idx,ctags);
  busIDs.upload();
  genIDs.upload();
  busIDs.getAllData(bus_ids);
  genIDs.getAllData(ctags);

  ngen = bus_ids.size();
  buses.clear();
  tags.clear();
  for (i=0; i<ngen; i++) {
    buses.push_back(bus_ids[i]);
    tags.push_back(ctags[i].str);
  }
}

/**
 * Execute application. argc and argv are standard runtime parameters
 */
void gridpack::rtpr::RTPRDriver::execute(int argc, char** argv)
{
  int i, j;
  // Get timer instance for timing entire calculation
  gridpack::utility::CoarseTimer *timer =
    gridpack::utility::CoarseTimer::instance();
  int t_total = timer->createCategory("Total Application");
  timer->start(t_total);

  // Read configuration file (user specified, otherwise assume that it is
  // call input.xml)
  gridpack::utility::Configuration *config
    = gridpack::utility::Configuration::configuration();
  if (argc >= 2 && argv[1] != NULL) {
    char inputfile[256];
    sprintf(inputfile,"%s",argv[1]);
    config->open(inputfile,p_world);
  } else {
    config->open("input.xml",p_world);
  }

  // Get size of group (communicator) that individual contingency calculations
  // will run on and create a task communicator. Each process is part of only
  // one task communicator, even though the world communicator is broken up into
  // many task communicators
  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = config->getCursor("Configuration.RealTimePathRating");
  int grp_size;
  // Check to find out if files should be printed for individual power flow
  // calculations
  std::string tmp_bool;
  gridpack::utility::StringUtils util;
  if (!cursor->get("printCalcFiles",&tmp_bool)) {
    p_print_calcs = true;
  } else {
    util.toLower(tmp_bool);
    if (tmp_bool == "false") {
      p_print_calcs = false;
    } else {
      p_print_calcs = true;
    }
  }
  if (!cursor->get("groupSize",&grp_size)) {
    grp_size = 1;
  }
  bool foundArea = true;
  bool found;
  found = cursor->get("sourceArea", &p_srcArea);
  if (!found) {
    p_srcArea = 0;
  }
  foundArea = found && foundArea;
  found = cursor->get("destinationArea", &p_dstArea);
  if (!found) {
    p_dstArea = 0;
  }
  foundArea = found && foundArea;
  if (!cursor->get("sourceZone",&p_srcZone)) {
    p_srcZone = 0;
  }
  if (!cursor->get("destinationZone",&p_dstZone)) {
    p_dstZone = 0;
  }
  bool calcGenCntngcy;
  if (!cursor->get("calculateGeneratorContingencies",&tmp_bool)) {
    calcGenCntngcy = false;
  } else {
    util.toLower(tmp_bool);
    if (tmp_bool == "false") {
      calcGenCntngcy = false;
    } else {
      calcGenCntngcy = true;
    }
  }
  bool calcLineCntngcy;
  if (!cursor->get("calculateLineContingencies",&tmp_bool)) {
    calcLineCntngcy = false;
  } else {
    util.toLower(tmp_bool);
    if (tmp_bool == "false") {
      calcLineCntngcy = false;
    } else {
      calcLineCntngcy = true;
    }
  }
  bool calcTieLines;
  if (!cursor->get("calculateTieLines",&tmp_bool)) {
    calcTieLines = false;
  } else {
    util.toLower(tmp_bool);
    if (tmp_bool == "false") {
      calcTieLines = false;
    } else {
      calcTieLines = true;
    }
  }
  if (!cursor->get("minVoltage",&p_Vmin)) {
    p_Vmin = 0.9;
  }
  if (!cursor->get("maxVoltage",&p_Vmax)) {
    p_Vmax = 1.1;
  }
  // Check for Q limit violations
  if (!cursor->get("checkQLimit",&p_check_Qlim)) {
    p_check_Qlim = false;
  }
  // TODO: Set these values from input deck
  double start = 0.1;
  double end = 10.0;
  double tstep = 0.005;

  // Create task communicators from world communicator
  p_task_comm = p_world.divide(grp_size);
  // Create powerflow applications on each task communicator
  p_pf_network.reset(new gridpack::powerflow::PFNetwork(p_task_comm));
  // Read in the network from an external file and partition it over the
  // processors in the task communicator. This will read in power flow
  // parameters from the Powerflow block in the input
  p_pf_app.readNetwork(p_pf_network,config);
  // Finish initializing the network
  p_pf_app.initialize();

  if (!calcGenCntngcy && !calcLineCntngcy) {
    // Read in contingency file name
    std::string contingencyfile;
    cursor = config->getCursor(
        "Configuration.RealTimePathRating");
    if (!cursor->get("contingencyList",&contingencyfile)) {
      contingencyfile = "contingencies.xml";
    }
    if (p_world.rank() == 0) {
      printf("Contingency File: %s\n",contingencyfile.c_str());
    }
    // Open contingency file
    bool ok = config->open(contingencyfile,p_world);

    // Get a list of contingencies. Set cursor so that it points to the
    // Contingencies block in the contingency file
    cursor = config->getCursor(
        "ContingencyList.RealTimePathRating.Contingencies");
    gridpack::utility::Configuration::ChildCursors contingencies;
    if (cursor) cursor->children(contingencies);
    p_events = getContingencies(contingencies);
  } else {
    std::vector<gridpack::powerflow::Contingency> genContingencies;
    if (calcGenCntngcy) {
      if (p_world.rank()==0) {
        printf("Calculating generator contingencies automatically"
            " for area %d",p_srcArea);
        if (p_srcZone > 0) {
          printf(" and zone %d\n",p_srcZone);
        } else {
          printf("\n");
        }
      }
      genContingencies = 
        createGeneratorContingencies(p_pf_network, p_srcArea, p_srcZone);
    }
    std::vector<gridpack::powerflow::Contingency> branchContingencies;
    if (calcLineCntngcy) {
      if (p_world.rank()==0) {
        printf("Calculating line contingencies automatically"
            " for area %d",p_srcArea);
        if (p_srcZone > 0) {
          printf(" and zone %d\n",p_srcZone);
        } else {
          printf("\n");
        }
      }
      branchContingencies =
        createBranchContingencies(p_pf_network, p_srcArea, p_srcZone);
    }
    p_events = genContingencies;
    int nsize = branchContingencies.size();
    int ic;
    for (ic = 0; ic<nsize; ic++) {
      p_events.push_back(branchContingencies[ic]);
    }
  }
  // copy power flow continencies to dynamic simulation events
  p_eventsDS.clear();
  for (i=0; i<p_events.size(); i++) {
    gridpack::dynamic_simulation::Event event;
    if (p_events[i].p_type == gridpack::powerflow::Generator) {
      event.from_idx = p_events[i].p_busid[0];
      event.to_idx = p_events[i].p_busid[0];
      event.bus_idx = p_events[i].p_busid[0];
      strncpy(event.tag,p_events[i].p_genid[0].c_str(),2);
      event.tag[2] = '\0';
      event.isGenerator = true;
      event.isLine = false;
    } else {
      event.from_idx = p_events[i].p_from[0];
      event.to_idx = p_events[i].p_to[0];
      strncpy(event.tag,p_events[i].p_ckt[0].c_str(),2);
      event.tag[2] = '\0';
      event.isGenerator = false;
      event.isLine = true;
    }
    event.start = start;
    event.end = end;
    event.step = tstep;
    p_eventsDS.push_back(event);
  }
  // Check for tie lines Set cursor so that it points to the
  // TieLines block in the input file
  std::vector<gridpack::rtpr::TieLine>  ties;
  if (!calcTieLines) {
    cursor = config->getCursor(
        "Configuration.RealTimePathRating.tieLines");
    gridpack::utility::Configuration::ChildCursors
      tielines;
    if (cursor) cursor->children(tielines);
    ties = getTieLines(tielines);
    // no tie lines found in file but areas (at least) were specified
    // so try calculating tie lines
    if (ties.size() == 0 && foundArea) {
      ties = getTieLines(p_srcArea,p_srcZone,p_dstArea,p_dstZone);
    }
  } else {
    ties = getTieLines(p_srcArea,p_srcZone,p_dstArea,p_dstZone);
  }
  int p_numTies = ties.size();
  p_from_bus.resize(p_numTies);
  p_to_bus.resize(p_numTies);
  p_tags.resize(p_numTies);
  for (i=0; i<p_numTies; i++) {
    p_from_bus[i] = ties[i].from;
    p_to_bus[i] = ties[i].to;
    p_tags[i] = ties[i].tag;
  }
  // Print out list of tie lines
  if (p_world.rank() == 0) {
    printf("List of Tie Lines\n");
    for (i=0; i<p_numTies; i++) {
      printf("From bus: %8d To bus: %8d Line ID: %s\n",p_from_bus[i],
          p_to_bus[i],p_tags[i].c_str());
    }
  }
  // Print out a list of contingencies from process 0
  // (the list is replicated on all processors)
  if (p_world.rank() == 0) {
    int idx;
    for (idx = 0; idx < p_events.size(); idx++) {
      printf("Name: %s\n",p_events[idx].p_name.c_str());
      if (p_events[idx].p_type == Branch) {
        int nlines = p_events[idx].p_from.size();
        int j;
        for (j=0; j<nlines; j++) {
          printf(" Line: (from) %d (to) %d (line) \'%s\'\n",
              p_events[idx].p_from[j],p_events[idx].p_to[j],
              p_events[idx].p_ckt[j].c_str());
        }
      } else if (p_events[idx].p_type == Generator) {
        int nbus = p_events[idx].p_busid.size();
        int j;
        for (j=0; j<nbus; j++) {
          printf(" Generator: (bus) %d (generator ID) \'%s\'\n",
              p_events[idx].p_busid[j],p_events[idx].p_genid[j].c_str());
        }
      }
    }
    if (p_events.size() == 0) {
      printf("***** No contingencies found *****\n");
    }
  }

  p_rating = 1.0;
  bool checkTie = runContingencies();
  if (checkTie) {
    // Tie lines are secure for all contingencies. Increase loads and generation
    while (checkTie) {
      p_rating += 0.05;
      p_pf_app.scaleLoadRealPower(p_rating,p_dstArea,p_dstZone);
      if (!p_pf_app.scaleGeneratorRealPower(p_rating,p_srcArea,p_srcZone)) {
        p_rating -= 0.05;
        p_pf_app.resetRealPower();
        break;
      }
      checkTie = runContingencies();
      if (!checkTie) p_rating -= 0.05;
      p_pf_app.resetRealPower();
    }
    // Refine estimate of rating
    checkTie = true;
    while (checkTie) {
      p_rating += 0.01;
      p_pf_app.scaleLoadRealPower(p_rating,p_dstArea,p_dstZone);
      if (!p_pf_app.scaleGeneratorRealPower(p_rating,p_srcArea,p_srcZone)) {
        p_rating -= 0.01;
        if (p_world.rank() == 0) {
          printf("Real power generation for power flow"
              " is capacity-limited for Rating: %f\n",
              p_rating);
        }
        p_pf_app.resetRealPower();
        break;
      }
      checkTie = runContingencies();
      if (!checkTie) p_rating -= 0.01;
      p_pf_app.resetRealPower();
    }
  } else {
    // Tie lines are insecure for some contingencies. Decrease loads and generation
    while (!checkTie && p_rating >= 0.0) {
      p_rating -= 0.05;
      p_pf_app.scaleLoadRealPower(p_rating,p_dstArea,p_dstZone);
      if (!p_pf_app.scaleGeneratorRealPower(p_rating,p_srcArea,p_srcZone)) {
        printf("Rating capacity exceeded: %d\n",p_rating);
        p_rating += 0.05;
        p_pf_app.resetRealPower();
        break;
      }
      checkTie = runContingencies();
      if (checkTie) p_rating += 0.05;
      p_pf_app.resetRealPower();
    }
    // Refine estimate of rating
    checkTie = false;
    while (!checkTie && p_rating >= 0.0) {
      p_rating -= 0.01;
      p_pf_app.scaleLoadRealPower(p_rating,p_dstArea,p_dstZone);
      if (!p_pf_app.scaleGeneratorRealPower(p_rating,p_srcArea,p_srcZone)) {
        p_rating += 0.01;
        if (p_world.rank() == 0) {
          printf("Real power generation for power flow"
              " is capacity-limited for Rating: %f\n",
              p_rating);
        }
        p_pf_app.resetRealPower();
        break;
      }
      checkTie = runContingencies();
      p_pf_app.resetRealPower();
    }
  }
  if (p_world.rank() == 0) {
    printf("Final Power Flow Rating: %f\n",p_rating);
  }
  // Power flow estimate of path rating is complete. Now check to see if the
  // rating is stable using dynamic simulation. Start by cloning the dynamic
  // simulation network from the power flow network
  p_ds_network.reset(new gridpack::dynamic_simulation::DSFullNetwork(p_task_comm));
  p_pf_network->clone<gridpack::dynamic_simulation::DSFullBus,
    gridpack::dynamic_simulation::DSFullBranch>(p_ds_network);

  // find the generators that will be monitored
  std::vector<int> busIDs;
  std::vector<std::string> genIDs;
  findWatchedGenerators(p_ds_network,p_srcArea,p_srcZone,busIDs,genIDs);
  if (p_world.rank() == 0) {
    printf("Generators being monitored in dynamic simulations\n");
    for (i=0; i<busIDs.size(); i++) {
      printf("    Host bus: %8d   Generator ID: %s\n",
          busIDs[i],genIDs[i].c_str());
    }
  }
  // initialize dynamic simulation
  p_ds_app.setNetwork(p_ds_network, config);
  p_ds_app.readGenerators();
  p_ds_app.initialize();
  p_ds_app.setGeneratorWatch(busIDs,genIDs,false);

  p_ds_app.scaleLoadRealPower(p_rating,p_dstArea,p_dstZone);
  p_ds_app.scaleGeneratorRealPower(p_rating,p_srcArea,p_srcZone);
  checkTie = runDSContingencies();
  p_ds_app.resetRealPower();

  // Current rating is an upper bound. Only check lower values.
  while (!checkTie && p_rating >= 0.0) {
    p_rating -= 0.01;
    p_ds_app.scaleLoadRealPower(p_rating,p_dstArea,p_dstZone);
    if (!p_ds_app.scaleGeneratorRealPower(p_rating,p_srcArea,p_srcZone)) {
      p_rating += 0.01;
      if (p_world.rank() == 0) {
        printf("Real power generation for dynamic simulation"
            " is capacity-limited for Rating: %f\n",
            p_rating);
      }
      p_ds_app.resetRealPower();
      break;
    }
    checkTie = runDSContingencies();
    p_ds_app.resetRealPower();
  }

  if (p_world.rank() == 0) {
    printf("Final Dynamic Simulation Rating: %f\n",p_rating);
  }

  timer->stop(t_total);
  // If all processors executed at least one task, then print out timing
  // statistics (this printout does not work if some processors do not define
  // all timing variables)
  if (p_events.size()*grp_size >= p_world.size()) {
    timer->dump();
  }
}

/**
 * Run complete set of contingencies
 * @return true if no tie line violations found
 */
bool gridpack::rtpr::RTPRDriver::runContingencies()
{
  bool ret = true;
  bool chkLines;
  bool chkSolve;
  // Keep track of failed calculations
  std::vector<int> contingency_idx;
  std::vector<bool> violations;
  std::vector<bool> contingency_success;
  // Keep track of which calculations completed successfully
  gridpack::parallel::GlobalVector<bool> ca_success(p_world);
  // Keep track of violation status for completed calculations
  // 1: no violations
  // 2: voltage violation
  // 3: line overload violation
  // 4: both voltage violation and line overload violation
  std::vector<int> contingency_violation;
  gridpack::parallel::GlobalVector<int> ca_violation(p_world);

  //  Set minimum and maximum voltage limits on all buses
  p_pf_app.setVoltageLimits(p_Vmin, p_Vmax);
  // Solve the base power flow calculation. This calculation is replicated on
  // all task communicators
  char sbuf[128];
  sprintf(sbuf,"base_%f.out",p_rating);
  if (p_print_calcs) p_pf_app.open(sbuf);
  sprintf(sbuf,"\nRunning base case on %d processes\n",p_task_comm.size());
  if (p_print_calcs) p_pf_app.writeHeader(sbuf);
  chkSolve = p_pf_app.solve();
  // Check for Qlimit violations
  if (p_check_Qlim && !p_pf_app.checkQlimViolations()) {
    chkSolve = p_pf_app.solve();
  }
  if (!chkSolve) printf("Failed solution on base case\n");
  // Some buses may violate the voltage limits in the base problem. Flag these
  // buses to ignore voltage violations on them.
  p_pf_app.ignoreVoltageViolations();

  // Check for tie line violations
  chkLines = p_pf_app.checkLineOverloadViolations(p_from_bus, p_to_bus,
      p_tags, violations);
  ret = ret && chkLines;
  if (!chkLines) printf("Line overload on base case\n");
  // Write out voltages and currents
  if (p_print_calcs) p_pf_app.write();
  if (p_print_calcs) p_pf_app.close();

  // Set up task manager on the world communicator. The number of tasks is
  // equal to the number of contingencies
  gridpack::parallel::TaskManager taskmgr(p_world);
  int ntasks = p_events.size();
  if (ntasks == 0) {
    return chkSolve;
  }
  taskmgr.set(ntasks);

  // Get bus voltage information for base case
  if (p_check_Qlim) p_pf_app.clearQlimViolations();

  // Evaluate contingencies using the task manager
  int task_id;
  // nextTask returns the same task_id on all processors in task_comm. When the
  // calculation runs out of task, nextTask will return false.
  while (taskmgr.nextTask(p_task_comm, &task_id)) {
    printf("Executing task %d on process %d\n",task_id,p_world.rank());
    sprintf(sbuf,"%s.out",p_events[task_id].p_name.c_str());
    // Open a new file, based on the contingency name, to store results from
    // this particular contingency calculation
    if (p_print_calcs) p_pf_app.open(sbuf);
    // Write out information to the top of the output file providing some
    // information on the contingency
    sprintf(sbuf,"\nRunning task on %d processes\n",p_task_comm.size());
    if (p_print_calcs) p_pf_app.writeHeader(sbuf);
    if (p_events[task_id].p_type == Branch) {
      int nlines = p_events[task_id].p_from.size();
      int j;
      for (j=0; j<nlines; j++) {
        sprintf(sbuf," Line: (from) %d (to) %d (line) \'%s\'\n",
            p_events[task_id].p_from[j],p_events[task_id].p_to[j],
            p_events[task_id].p_ckt[j].c_str());
        printf("p[%d] Line: (from) %d (to) %d (line) \'%s\'\n",
            p_pf_network->communicator().rank(),
            p_events[task_id].p_from[j],p_events[task_id].p_to[j],
            p_events[task_id].p_ckt[j].c_str());
      }
    } else if (p_events[task_id].p_type == Generator) {
      int nbus = p_events[task_id].p_busid.size();
      int j;
      for (j=0; j<nbus; j++) {
        sprintf(sbuf," Generator: (bus) %d (generator ID) \'%s\'\n",
            p_events[task_id].p_busid[j],p_events[task_id].p_genid[j].c_str());
        printf("p[%d] Generator: (bus) %d (generator ID) \'%s\'\n",
            p_pf_network->communicator().rank(),
            p_events[task_id].p_busid[j],p_events[task_id].p_genid[j].c_str());
      }
    }
    if (p_print_calcs) p_pf_app.writeHeader(sbuf);
    // Reset all voltages back to their original values
    p_pf_app.resetVoltages();
    // Set contingency
    p_pf_app.setContingency(p_events[task_id]);
    // Solve power flow equations for this system
#ifdef USE_SUCCESS
    contingency_idx.push_back(task_id);
#endif
    if (p_pf_app.solve()) {
      chkSolve = true;
#ifdef USE_SUCCESS
      contingency_success.push_back(true);
#endif
      if (p_check_Qlim && !p_pf_app.checkQlimViolations()) {
        chkSolve = p_pf_app.solve();
      }
      if (!chkSolve) printf("Failed solution on continency %d\n",task_id+1);
      // If power flow solution is successful, write out voltages and currents
      if (p_print_calcs) p_pf_app.write();
      chkLines = p_pf_app.checkLineOverloadViolations(p_from_bus, p_to_bus, p_tags,
          violations);
      ret = ret && chkLines;
      // Check for violations
      bool ok1 = p_pf_app.checkVoltageViolations();
      bool ok2 = p_pf_app.checkLineOverloadViolations();
      bool ok = ok1 && ok2;
      // Include results of violation checks in output
      if (ok) {
        sprintf(sbuf,"\nNo violation for contingency %s\n",
            p_events[task_id].p_name.c_str());
#ifdef USE_SUCCESS
        contingency_violation.push_back(1);
#endif
      } 
      if (!ok1) {
        sprintf(sbuf,"\nBus Violation for contingency %s\n",
            p_events[task_id].p_name.c_str());
      }
      if (p_print_calcs) p_pf_app.print(sbuf);
      if (p_print_calcs) p_pf_app.writeCABus();
      if (!ok2) {
        sprintf(sbuf,"\nBranch Violation for contingency %s\n",
            p_events[task_id].p_name.c_str());
      }

#ifdef USE_SUCCESS
      if (!ok1 && !ok2) {
        contingency_violation.push_back(4);
      } else if (!ok1) {
        contingency_violation.push_back(2);
      } else if (!ok2) {
        contingency_violation.push_back(3);
      }
#endif
        
      if (p_print_calcs) p_pf_app.print(sbuf);
      if (p_print_calcs) p_pf_app.writeCABranch();
      if (p_check_Qlim) p_pf_app.clearQlimViolations();
    } else {
      if (!chkSolve) printf("Failed solution on continency %d\n",task_id+1);
#ifdef USE_SUCCESS
      contingency_success.push_back(false);
      contingency_violation.push_back(0);
#endif
      sprintf(sbuf,"\nDivergent for contingency %s\n",
          p_events[task_id].p_name.c_str());
      if (p_print_calcs) p_pf_app.print(sbuf);
    } 
    // Return network to its original base case state
    p_pf_app.unSetContingency(p_events[task_id]);
    // Close output file for this contingency
    if (p_print_calcs) p_pf_app.close();
  }
  // Print statistics from task manager describing the number of tasks performed
  // per processor
  taskmgr.printStats();

  // Gather stats on successful contingency calculations
#ifdef USE_SUCCESS
  if (p_task_comm.rank() == 0) {
    ca_success.addElements(contingency_idx, contingency_success);
    ca_violation.addElements(contingency_idx, contingency_violation);
  }
  ca_success.upload();
  ca_violation.upload();
  // Write out stats on successful calculations
  if (p_world.rank() == 0) {
    contingency_idx.clear();
    contingency_success.clear();
    contingency_violation.clear();
    for (i=0; i<ntasks; i++) contingency_idx.push_back(i);
    ca_success.getData(contingency_idx, contingency_success);
    contingency_success.clear();
    ca_violation.getData(contingency_idx, contingency_violation);
    std::ofstream fout;
    fout.open("success.txt");
    for (i=0; i<ntasks; i++) {
      if (contingency_success[i]) {
        fout << "contingency: " << i+1 << " success: true";
        if (contingency_violation[i] == 1) {
          fout << " violation: none" << std::endl;
        } else if (contingency_violation[i] == 2) {
          fout << " violation: bus" << std::endl;
        } else if (contingency_violation[i] == 3) {
          fout << " violation: branch" << std::endl;
        } else if (contingency_violation[i] == 4) {
          fout << " violation: bus and branch" << std::endl;
        }
      } else {
        fout << "contingency: " << i+1 << " success: false" << std::endl;
      }
    }
    fout.close();
  }
#endif
  // Check to see if ret is true on all processors
  int ok;
  if (ret) {
    ok = 1;
  } else {
    ok = 0;
  }
  p_world.sum(&ok,1);
  if (ok == p_world.size()) {
    ret = true;
  } else {
    ret = false;
  }
  return ret;
}

/**
 * Run dynamic simulations over full set of contingencies
 * @return true if no violations found on complete set of contingencies
 */
bool gridpack::rtpr::RTPRDriver::runDSContingencies()
{
  bool ret = true;
  // Set up task manager on the world communicator. The number of tasks is
  // equal to the number of contingencies
  gridpack::parallel::TaskManager taskmgr(p_world);
  int ntasks = p_eventsDS.size();
  taskmgr.set(ntasks);

  // Evaluate contingencies using the task manager
  int task_id;
  // nextTask returns the same task_id on all processors in task_comm. When the
  // calculation runs out of task, nextTask will return false.
  while (taskmgr.nextTask(p_task_comm, &task_id)) {
    printf("Executing dynamic simulation task %d on process %d\n",
        task_id,p_world.rank());
    try {
      p_ds_app.solve(p_eventsDS[task_id]);
    } catch (const gridpack::Exception e) {
      printf("Failed to execute DS task %d on process %d\n",
        task_id,p_world.rank());
    }
    ret = ret && p_ds_app.frequencyOK();
  
  }
  int iret = static_cast<int>(ret);
  p_world.sum(&iret,1);
  if (iret == p_world.size()) {
    ret = true;
  } else {
    ret = false;
  }
  return ret;
}