/*
 *     Copyright (c) 2013 Battelle Memorial Institute
 *     Licensed under modified BSD License. A copy of this license can be found
 *     in the LICENSE file in the top level directory of this distribution.
 */
// -------------------------------------------------------------
/**
 * @file   ds_app.cpp
 * @author Shuangshuang Jin
 * @Last modified:  May 13, 2015
 *
 * @brief
 *
 *
 */
// -------------------------------------------------------------
//
//#define USE_TIMESTAMP

#include "gridpack/include/gridpack.hpp"
#include "dsf_app_module.hpp"

//#define MAP_PROFILE

// Calling program for dynamic simulation application

/**
 * Basic constructor
 */
gridpack::dynamic_simulation::DSFullApp::DSFullApp(void)
{
  p_internal_watch_file_name = false; 
  p_generatorWatch = false;
  p_loadWatch = false;
  p_generators_read_in = false;
}

/**
 * Basic constructor with commmunicator argument
 * @param comm communicator that application object is restricted to
 */
gridpack::dynamic_simulation::DSFullApp::DSFullApp(gridpack::parallel::Communicator comm)
: p_comm(comm)
{
  p_generatorWatch = false;
  p_loadWatch = false;
  p_generators_read_in = false;
}

/**
 * Basic destructor
 */
gridpack::dynamic_simulation::DSFullApp::~DSFullApp(void)
{
}

/**
 * Read in and partition the dynamic simulation network. The input file is read
 * directly from the Dynamic_simulation block in the configuration file so no
 * external file names or parameters need to be passed to this routine
 * @param network pointer to a DSFullNetwork object. This should not have any
 * buses or branches defined on it.
 * @param config pointer to open configuration file
 * @param otherfile name of network configuration file if different from the
 * one in the input deck
 */
void gridpack::dynamic_simulation::DSFullApp::readNetwork(
    boost::shared_ptr<DSFullNetwork> &network,
    gridpack::utility::Configuration *config,
    const char *otherfile)
{
  p_comm = network->communicator();
  p_network = network;
  p_config = config;

  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = p_config->getCursor("Configuration.Dynamic_simulation");
  std::string filename;
  if (otherfile == NULL) {
    if (!cursor->get("networkConfiguration",&filename)) {
      printf("No network configuration specified\n");
    }
  } else {
    filename = otherfile;
  }

  p_sim_time = cursor->get("simulationTime",0.0);
  if (p_sim_time == 0.0) {
    // TODO: some kind of error
  }
  p_time_step = cursor->get("timeStep",0.0);
  if (p_time_step == 0.0) {
    // TODO: some kind of error
  }

  // load input file
  gridpack::parser::PTI23_parser<DSFullNetwork> parser(network);
  parser.parse(filename.c_str());
  cursor = config->getCursor("Configuration.Dynamic_simulation");
  filename = cursor->get("generatorParameters","");
  if (filename.size() > 0) parser.parse(filename.c_str());

  // partition network
  network->partition();

  // Create serial IO object to export data from buses or branches
  p_busIO.reset(new gridpack::serial_io::SerialBusIO<DSFullNetwork>(512, network));
  p_branchIO.reset(new gridpack::serial_io::SerialBranchIO<DSFullNetwork>(128, network));
}

/**
 * Assume that DSFullNetwork already exists and just cache an internal pointer
 * to it. This routine does not call the partition function. Also read in
 * simulation parameters from configuration file
 * @param network pointer to a complete DSFullNetwork object.
 * @param config pointer to open configuration file
 */
void gridpack::dynamic_simulation::DSFullApp::setNetwork(
    boost::shared_ptr<DSFullNetwork> &network,
    gridpack::utility::Configuration *config)
{
  p_comm = network->communicator();
  p_network = network;
  p_config = config;

  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = config->getCursor("Configuration.Dynamic_simulation");
  std::string filename = cursor->get("networkConfiguration", 
      "No network configuration specified");
  p_sim_time = cursor->get("simulationTime",0.0);
  if (p_sim_time == 0.0) {
    // TODO: some kind of error
  }
  p_time_step = cursor->get("timeStep",0.0);
  if (p_time_step == 0.0) {
    // TODO: some kind of error
  }

  // Create serial IO object to export data from buses or branches
  p_busIO.reset(new gridpack::serial_io::SerialBusIO<DSFullNetwork>(512, network));
  p_branchIO.reset(new gridpack::serial_io::SerialBranchIO<DSFullNetwork>(128, network));
}

/**
 * Read generator parameters. These will come from a separate file (most
 * likely). The name of this file comes from the input configuration file.
 */
void gridpack::dynamic_simulation::DSFullApp::readGenerators(void)
{
  int rank = p_network->communicator().rank();
  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = p_config->getCursor("Configuration.Dynamic_simulation");
  gridpack::parser::PTI23_parser<DSFullNetwork> parser(p_network);
  cursor = p_config->getCursor("Configuration.Dynamic_simulation");
  std::string filename = cursor->get("generatorParameters","");
  if (filename.size() > 0) parser.externalParse(filename.c_str());
}

/**
 * Check to see if system is secure
 */
int gridpack::dynamic_simulation::DSFullApp::isSecure()
{
  return p_insecureAt;
}

/**
 * Set up exchange buffers and other internal parameters and
 * initialize
 * network components using data from data collection
 */
void gridpack::dynamic_simulation::DSFullApp::initialize()
{
  // create factory
  p_factory.reset(new gridpack::dynamic_simulation::DSFullFactory(p_network));
  p_factory->load();

  // set network components using factory
  p_factory->setComponents();

  // set voltages for the extended buses from composite load model
  p_factory->setExtendedCmplBusVoltage();

  // load parameters for the extended buses from composite load model
  p_factory->LoadExtendedCmplBus();

  // set YBus components so that you can create Y matrix  
  p_factory->setYBus();

  if (!p_factory->checkGen()) {
    p_busIO->header("Missing generators on at least one processor\n");
    return;
  }
}

/**
 * Reinitialize calculation from data collections
 */
void gridpack::dynamic_simulation::DSFullApp::reload()
{
  p_factory->load();
  p_factory->setYBus();
}

/**
 * Execute the time integration portion of the application
 */
void gridpack::dynamic_simulation::DSFullApp::solve(
    gridpack::dynamic_simulation::DSFullBranch::Event fault)
{
  gridpack::utility::CoarseTimer *timer =
    gridpack::utility::CoarseTimer::instance();
  int t_solve = timer->createCategory("DS Solve: Total");
  int t_misc = timer->createCategory("DS Solve: Miscellaneous");
#ifdef MAP_PROFILE
  timer->configTimer(false);
#endif
  timer->start(t_solve);
  timer->start(t_misc);

  // Get cursor for setting solver options
  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = p_config->getCursor("Configuration.Dynamic_simulation");
  timer->stop(t_misc);

  int t_mode = timer->createCategory("DS Solve: Set Mode");
  timer->start(t_mode);
  p_factory->setMode(YBUS);
  timer->stop(t_mode);
  int t_ybus = timer->createCategory("DS Solve: Make YBus");
  timer->start(t_ybus);
  gridpack::mapper::FullMatrixMap<DSFullNetwork> ybusMap(p_network);
  boost::shared_ptr<gridpack::math::Matrix> orgYbus = ybusMap.mapToMatrix();

  // Form constant impedance load admittance yl for all buses and add it to
  // system Y matrix: ybus = ybus + yl
  p_factory->setMode(YL);
  boost::shared_ptr<gridpack::math::Matrix> ybusyl = ybusMap.mapToMatrix();
  timer->stop(t_ybus);

  p_factory->setMode(PG);
  boost::shared_ptr<gridpack::math::Matrix> ybuspg = ybusMap.mapToMatrix();

  // Add j*Xd' to system Y matrix:
  // Extract appropriate xdprime and xdpprime from machine data
  timer->start(t_mode);
  p_factory->setMode(jxd);
  timer->stop(t_mode);
  timer->start(t_ybus);
  boost::shared_ptr<gridpack::math::Matrix> ybus_jxd = ybusMap.mapToMatrix();
  timer->stop(t_ybus);

  // Add dynamic load impedance to system Y matrix:
  timer->start(t_mode);
  p_factory->setMode(YDYNLOAD);
  timer->stop(t_mode);
  timer->start(t_ybus);
  boost::shared_ptr<gridpack::math::Matrix> ybus = ybusMap.mapToMatrix();

  // Get fault information from fautlts Event from input.xml
  int sw2_2 = fault.from_idx - 1;
  int sw3_2 = fault.to_idx - 1;

  // Compute ybus_fy for fault on stage
  boost::shared_ptr<gridpack::math::Matrix> ybus_fy(ybus->clone());
  timer->stop(t_ybus);
  timer->start(t_misc);
  p_factory->setEvent(fault);
  timer->stop(t_misc);
  timer->start(t_mode);
  p_factory->setMode(onFY);
  timer->stop(t_mode);
  timer->start(t_ybus);
  ybusMap.overwriteMatrix(ybus_fy);

  // Compute ybus_posfy for fault clear stage
  boost::shared_ptr<gridpack::math::Matrix> ybus_posfy(ybus->clone());
  timer->stop(t_ybus);
  timer->start(t_mode);
  p_factory->setMode(posFY);
  timer->stop(t_mode);
  timer->start(t_ybus);
  ybusMap.incrementMatrix(ybus_posfy);
  timer->stop(t_ybus);

  // Simulation related variables
  int t_init = timer->createCategory("DS Solve: Initialization");
  timer->start(t_init);
  int simu_k;
  int t_step[20];
  double t_width[20];
  int S_Steps;
  int last_S_Steps;
  int steps3, steps2, steps1;
  double h_sol1, h_sol2;
  int flagP, flagC;
  int I_Steps;

  const double sysFreq = 60.0;
  double pi = 4.0*atan(1.0);
  const double basrad = 2.0 * pi * sysFreq;
  gridpack::ComplexType jay(0.0, 1.0);

  // switch info is set up here
  int nswtch = 4;
  static double sw1[4];
  static double sw7[4];
  sw1[0] = 0.0;
  sw1[1] = fault.start;
  sw1[2] = fault.end;
  sw1[3] = p_sim_time;
  sw7[0] = p_time_step;
  sw7[1] = fault.step;
  sw7[2] = p_time_step;
  sw7[3] = p_time_step;
  simu_k = 0;
  for (int i = 0; i < nswtch-1; i++) {
    t_step[i] = (int) ((sw1[i+1] -sw1[i]) / sw7[i]);
    t_width[i] = (sw1[i+1] - sw1[i]) / t_step[i];
    simu_k += t_step[i];
  }
  simu_k++;

  // Initialize vectors for integration 
  p_factory->initDSVect(p_time_step);
  //exit(0);

  gridpack::mapper::BusVectorMap<DSFullNetwork> ngenMap(p_network);
  // Map to create vector volt
  boost::shared_ptr<gridpack::math::Vector> volt = ngenMap.mapToVector();

  gridpack::math::LinearSolver solver(*ybus);
  solver.configure(cursor);
  gridpack::math::LinearSolver solver_fy(*ybus_fy);
  solver_fy.configure(cursor);
  gridpack::math::LinearSolver solver_posfy(*ybus);
  solver_posfy.configure(cursor);

  steps3 = t_step[0] + t_step[1] + t_step[2] - 1;
  steps2 = t_step[0] + t_step[1] - 1;
  steps1 = t_step[0] - 1;
  h_sol1 = t_width[0];
  h_sol2 = h_sol1;
  flagP = 0;
  flagC = 0;
  S_Steps = 1;
  last_S_Steps = -1;

  bool flag = true, flag_corrector = true;

  p_insecureAt = -1;

  p_factory->setMode(make_INorton_full);
  gridpack::mapper::BusVectorMap<DSFullNetwork> nbusMap(p_network);
  boost::shared_ptr<gridpack::math::Vector> INorton_full = nbusMap.mapToVector();
  boost::shared_ptr<gridpack::math::Vector> INorton_full_chk = nbusMap.mapToVector();
  double max_INorton_full = 0.0;
  boost::shared_ptr<gridpack::math::Vector> volt_full(INorton_full->clone());

  timer->stop(t_init);
#ifdef USE_TIMESTAMP
  if (p_generatorWatch) p_generatorIO->header("t, t_stamp");//bus_id,ckt,x1d_1,x2w_1,x3Eqp_1,x4Psidp_1,x5Psiqpp_1");
  if (p_generatorWatch) p_generatorIO->write("watch_header");
  if (p_generatorWatch) p_generatorIO->header("\n");

  if (p_loadWatch) p_loadIO->header("t, t_stamp");
  if (p_loadWatch) p_loadIO->write("load_watch_header");
  if (p_loadWatch) p_loadIO->header("\n");
#else
  if (p_generatorWatch) p_generatorIO->header("t");
  if (p_generatorWatch) p_generatorIO->write("watch_header");
  if (p_generatorWatch) p_generatorIO->header("\n");

  if (p_loadWatch) p_loadIO->header("t");
  if (p_loadWatch) p_loadIO->write("load_watch_header");
  if (p_loadWatch) p_loadIO->header("\n");
#endif
#ifdef USE_GOSS
  if (p_generatorWatch) p_generatorIO->dumpChannel();
  if (p_loadWatch) p_loadIO->dumpChannel();
#endif
  for (I_Steps = 0; I_Steps < simu_k - 1; I_Steps++) {
    timer->start(t_misc);
    S_Steps = I_Steps;

    if (I_Steps < steps1) {
      flagP = 0;
      flagC = 0;
    } else if (I_Steps == steps1) {
      flagP = 0;
      //flagC = 1;
      flagC = 0;
    } else if ((I_Steps > steps1) && (I_Steps < steps2)) {
      flagP = 1;
      flagC = 1;
    } else if (I_Steps == steps2) {
      flagP = 1;
      //flagC = 2;
      flagC = 1;
    } else if (I_Steps > steps2) {
      flagP = 2;
      flagC = 2;
    }
    timer->stop(t_misc);

    if (I_Steps !=0 && last_S_Steps != S_Steps) {
      p_factory->predictor_currentInjection(false);
    } else {
      p_factory->predictor_currentInjection(true);
    }

#ifdef MAP_PROFILE
    timer->configTimer(true);
#endif
    int t_mIf = timer->createCategory("DS Solve: Modified Euler Predictor: Make INorton");
    timer->start(t_mIf);
    p_factory->setMode(make_INorton_full);
    nbusMap.mapToVector(INorton_full);
    timer->stop(t_mIf);
#ifdef MAP_PROFILE
    timer->configTimer(false);
#endif

    // ---------- CALL ssnetwork_cal_volt(S_Steps+1, flagF2) 
    // to calculate terminal volt: ----------
    int t_psolve = timer->createCategory("DS Solve: Modified Euler Predictor: Linear Solver");
    timer->start(t_psolve);
    volt_full->zero();
    if (flagP == 0) {
      solver.solve(*INorton_full, *volt_full);
    } else if (flagP == 1) {
      solver_fy.solve(*INorton_full, *volt_full);
    } else if (flagP == 2) {
      solver_posfy.solve(*INorton_full, *volt_full);
    }
    timer->stop(t_psolve);

#ifdef MAP_PROFILE
    timer->configTimer(true);
#endif

    int t_vmap= timer->createCategory("DS Solve: Map Volt to Bus");
    timer->start(t_vmap);

    nbusMap.mapToBus(volt_full);

    if ( I_Steps==0 ) {
      p_factory->updateoldbusvoltage(); //renke add, first timestep, copy volt_full to volt_full_old
    }
    timer->stop(t_vmap);

    int t_volt= timer->createCategory("DS Solve: Set Volt");
    timer->start(t_volt);
    p_factory->setVolt(false);
    p_factory->updateBusFreq(h_sol1);
    timer->stop(t_volt);

    bool flagBus = p_factory->updateBusRelay(false, h_sol1);
    bool flagBranch = p_factory->updateBranchRelay(false, h_sol1);

    // update dynamic load internal relay functions here
    p_factory->dynamicload_post_process(h_sol1, false);

    // if bus relay trips, modify the corresponding Ymatrix, renke modified
    if (flagBus) {

      //please update the bus contribution to the Y bus matrix here. //Shuangshuang tbd
      if (flagP == 0) { 
        p_factory->setMode(bus_relay);
        ybusMap.overwriteMatrix(ybus);
      } else if (flagP == 1) {
        p_factory->setMode(bus_relay);
        ybusMap.overwriteMatrix(ybus_fy);
        ybus_fy->print();
        char sybus[100];
        sprintf(sybus, "ybus_fy_%d_relay.m",I_Steps );

        ybus_fy->save(sybus);

        p_factory->setMode(bus_relay);
        ybusMap.overwriteMatrix(ybus);
        ybus->print();
        sprintf(sybus, "ybus_%d_relay.m",I_Steps );

        ybus->save(sybus);

        p_factory->setMode(bus_relay);
        ybusMap.overwriteMatrix(ybus_posfy);
        ybus_posfy->print();
        sprintf(sybus, "ybus_posfy_%d_relay.m",I_Steps );

        ybus_posfy->save(sybus);


      } else if (flagP == 2) {
        p_factory->setMode(bus_relay);
        ybusMap.overwriteMatrix(ybus);
        ybus->print();
        char sybus[100];
        sprintf(sybus, "ybus_%d_relay.m",I_Steps );

        ybus->save(sybus);

        p_factory->setMode(bus_relay);
        ybusMap.overwriteMatrix(ybus_posfy);
        ybus_posfy->print();
        sprintf(sybus, "ybus_posfy_%d_relay.m",I_Steps );

        ybus_posfy->save(sybus);

      }
    }

    // if branch relay trips, modify the corresponding Ymatrix, renke modified
    if (flagBranch) {


      //please update the bus contribution to the Y bus matrix here. //Shuangshuang tbd
      if (flagP == 0) { 
        p_factory->setMode(branch_relay);
        ybusMap.incrementMatrix(ybus);
      } else if (flagP == 1) {
        p_factory->setMode(branch_relay);
        ybusMap.incrementMatrix(ybus_fy);
        ybus_fy->print();
        char sybus[100];
        sprintf(sybus, "ybus_fy_%d_relay.m",I_Steps );

        ybus_fy->save(sybus);

        p_factory->setMode(branch_relay);
        ybusMap.incrementMatrix(ybus);
        ybus->print();

        ybus->save(sybus);

        p_factory->setMode(branch_relay);
        ybusMap.incrementMatrix(ybus_posfy);
        ybus_posfy->print();
        sprintf(sybus, "ybus_posfy_%d_relay.m",I_Steps );

        ybus_posfy->save(sybus);

      } else if (flagP == 2) {
        p_factory->setMode(branch_relay);
        ybusMap.incrementMatrix(ybus);
        ybus->print();
        char sybus[100];
        sprintf(sybus, "ybus_%d_relay.m",I_Steps );

        ybus->save(sybus);

        p_factory->setMode(branch_relay);
        ybusMap.incrementMatrix(ybus_posfy);
        ybus_posfy->print();
        sprintf(sybus, "ybus_posfy_%d_relay.m",I_Steps );

        ybus_posfy->save(sybus);

      }
    }

    //renke add, update old busvoltage first
    p_factory->updateoldbusvoltage(); //renke add

#ifdef MAP_PROFILE
    timer->configTimer(false);
#endif

    int t_predictor = timer->createCategory("DS Solve: Modified Euler Predictor");
    timer->start(t_predictor);
    if (I_Steps !=0 && last_S_Steps != S_Steps) {
      p_factory->predictor(h_sol1, false);
    } else { 
      p_factory->predictor(h_sol1, true);
    }
    timer->stop(t_predictor);

    if (I_Steps !=0 && last_S_Steps != S_Steps) {
      p_factory->corrector_currentInjection(false);
    } else {
      p_factory->corrector_currentInjection(true);
    }

    int t_cmIf = timer->createCategory("DS Solve: Modified Euler Corrector: Make INorton");
    timer->start(t_cmIf);
    p_factory->setMode(make_INorton_full);
    nbusMap.mapToVector(INorton_full);
    timer->stop(t_cmIf);

    // ---------- CALL ssnetwork_cal_volt(S_Steps+1, flagF2)
    // to calculate terminal volt: ----------
    int t_csolve = timer->createCategory("DS Solve: Modified Euler Corrector: Linear Solver");
    timer->start(t_csolve);
    volt_full->zero();

    if (flagP == 0) {
      solver.solve(*INorton_full, *volt_full);
    } else if (flagP == 1) {
      solver_fy.solve(*INorton_full, *volt_full);
    } else if (flagP == 2) {
      solver_posfy.solve(*INorton_full, *volt_full);
    }

    timer->stop(t_csolve);

    timer->start(t_vmap);

    nbusMap.mapToBus(volt_full);

    timer->stop(t_vmap);

    timer->start(t_volt);
    p_factory->setVolt(false);
    p_factory->updateBusFreq(h_sol1);
    timer->stop(t_volt);

    int t_corrector = timer->createCategory("DS Solve: Modified Euler Corrector");
    timer->start(t_corrector);
    if (last_S_Steps != S_Steps) {
      p_factory->corrector(h_sol2, false);
    } else {
      p_factory->corrector(h_sol2, true);
    }
    timer->stop(t_corrector);


    if (I_Steps == steps1) {
      solver_fy.solve(*INorton_full, *volt_full);
      nbusMap.mapToBus(volt_full);
      p_factory->setVolt(false);
      p_factory->updateBusFreq(h_sol1);
    } else if (I_Steps == steps2) {
      solver_posfy.solve(*INorton_full, *volt_full);
      nbusMap.mapToBus(volt_full);
      p_factory->setVolt(true);
      p_factory->updateBusFreq(h_sol1);
    }
    int t_secure = timer->createCategory("DS Solve: Check Security");
    timer->start(t_secure);
    if (p_generatorWatch && I_Steps%p_generatorWatchFrequency == 0) {
      char tbuf[32];
#ifdef USE_TIMESTAMP
      sprintf(tbuf,"%8.4f, %20.4f",static_cast<double>(I_Steps)*p_time_step,
          timer->currentTime());
      if (p_generatorWatch) p_generatorIO->header(tbuf);
      if (p_generatorWatch) p_generatorIO->write("watch");
      if (p_generatorWatch) p_generatorIO->header("\n");
#else
      sprintf(tbuf,"%8.4f",static_cast<double>(I_Steps)*p_time_step);
      if (p_generatorWatch) p_generatorIO->header(tbuf);
      if (p_generatorWatch) p_generatorIO->write("watch");
      if (p_generatorWatch) p_generatorIO->header("\n");
#endif
#ifdef USE_GOSS
      if (p_generatorWatch) p_generatorIO->dumpChannel();
#endif
    }
    if (p_loadWatch && I_Steps%p_loadWatchFrequency == 0) {
      char tbuf[32];
#ifdef USE_TIMESTAMP
      sprintf(tbuf,"%8.4f, %20.4f",static_cast<double>(I_Steps)*p_time_step,
          timer->currentTime());
      if (p_loadWatch) p_loadIO->header(tbuf);
      if (p_loadWatch) p_loadIO->write("load_watch");
      if (p_loadWatch) p_loadIO->header("\n");
#else
      sprintf(tbuf,"%8.4f",static_cast<double>(I_Steps)*p_time_step);
      if (p_loadWatch) p_loadIO->header(tbuf);
      if (p_loadWatch) p_loadIO->write("load_watch");
      if (p_loadWatch) p_loadIO->header("\n");
#endif
#ifdef USE_GOSS
      if (p_loadWatch) p_loadIO->dumpChannel();
#endif
    }
    if ((!p_factory->securityCheck()) && p_insecureAt == -1)  
      p_insecureAt = I_Steps;
    last_S_Steps = S_Steps;
    timer->stop(t_secure);
  }

  char secureBuf[128];
  if (p_insecureAt == -1) sprintf(secureBuf,"\nThe system is secure!\n");
  else sprintf(secureBuf,"\nThe system is insecure from step %d!\n", p_insecureAt);
  p_busIO->header(secureBuf);

#ifdef MAP_PROFILE
  timer->configTimer(true);
#endif
  timer->stop(t_solve);
}

/**
 * Write out final results of dynamic simulation calculation to
 * standard output
 */
void gridpack::dynamic_simulation::DSFullApp::write(const char* signal)
{
  p_busIO->write(signal);
}

/**
 * Read faults from external file and form a list of faults
 * @param cursor pointer to open file contain fault or faults
 * @return a list of fault events
 */
std::vector<gridpack::dynamic_simulation::DSFullBranch::Event>
gridpack::dynamic_simulation::DSFullApp::
getFaults(gridpack::utility::Configuration::CursorPtr cursor)
{
  gridpack::utility::Configuration::CursorPtr list;
  list = cursor->getCursor("faultEvents");
  gridpack::utility::Configuration::ChildCursors events;
  std::vector<gridpack::dynamic_simulation::DSFullBranch::Event> ret;
  if (list) {
    list->children(events);
    int size = events.size();
    int idx;
    // Parse fault events
    for (idx=0; idx<size; idx++) {
      gridpack::dynamic_simulation::DSFullBranch::Event event;
      event.start = events[idx]->get("beginFault",0.0);
      event.end = events[idx]->get("endFault",0.0);
      std::string indices = events[idx]->get("faultBranch","0 0");
      //Parse indices to get from and to indices of branch
      int ntok1 = indices.find_first_not_of(' ',0);
      int ntok2 = indices.find(' ',ntok1);
      if (ntok2 - ntok1 > 0 && ntok1 != std::string::npos && ntok2 !=
          std::string::npos) {
        event.from_idx = atoi(indices.substr(ntok1,ntok2-ntok1).c_str());
        ntok1 = indices.find_first_not_of(' ',ntok2);
        ntok2 = indices.find(' ',ntok1);
        if (ntok1 != std::string::npos && ntok1 < indices.length()) {
          if (ntok2 == std::string::npos) {
            ntok2 = indices.length();
          }
          event.to_idx = atoi(indices.substr(ntok1,ntok2-ntok1).c_str());
        } else {
          event.from_idx = 0;
          event.to_idx = 0;
        }
      } else {
        event.from_idx = 0;
        event.to_idx = 0;
      }
      event.step = events[idx]->get("timeStep",0.0);
      if (event.step != 0.0 && event.end != 0.0 && event.from_idx != event.to_idx) {
        ret.push_back(event);
      }
    }
  }
  return ret;
}

/**
 * Read in generators that should be monitored during simulation
 */
void gridpack::dynamic_simulation::DSFullApp::setGeneratorWatch()
{
  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = p_config->getCursor("Configuration.Dynamic_simulation");
  if (!cursor->get("generatorWatchFrequency",&p_generatorWatchFrequency)) {
    p_generatorWatchFrequency = 1;
  }
  char buf[128];
  cursor = p_config->getCursor("Configuration.Dynamic_simulation.generatorWatch");
  gridpack::utility::Configuration::ChildCursors generators;
  if (cursor) cursor->children(generators);
  int i, j, idx, id, len;
  int ncnt = generators.size();
  std::string generator, tag, clean_tag;
  gridpack::dynamic_simulation::DSFullBus *bus;
  if (ncnt > 0) p_busIO->header("Monitoring generators:\n");
  for (i=0; i<ncnt; i++) {
    // Parse contents of "generator" field to get bus ID and generator tag
    generators[i]->get("busID",&id);
    generators[i]->get("generatorID",&tag);
    gridpack::utility::StringUtils util;
    clean_tag = util.clean2Char(tag);
    // Find local bus indices for generator
    std::vector<int> local_ids = p_network->getLocalBusIndices(id);
    for (j=0; j<local_ids.size(); j++) {
      bus = dynamic_cast<gridpack::dynamic_simulation::DSFullBus*>
        (p_network->getBus(local_ids[j]).get());
      bus->setWatch(clean_tag,true);
    }
    sprintf(buf,"  Bus: %8d Generator ID: %2s\n",id,clean_tag.c_str());
    p_busIO->header(buf);
    if (ncnt > 0) {
      p_generators_read_in = true;
      p_generatorWatch = true;
      sprintf(buf,"Generator Watch Frequency: %d\n",p_generatorWatchFrequency);
      p_busIO->header(buf);
    }
  }
  openGeneratorWatchFile();
}

void gridpack::dynamic_simulation::DSFullApp::setGeneratorWatch(const char *filename)
{
  p_gen_watch_file = filename;
  p_internal_watch_file_name = true;
  setGeneratorWatch();
}

/**
 * Read in loads that should be monitored during simulation
 */
void gridpack::dynamic_simulation::DSFullApp::setLoadWatch()
{
  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = p_config->getCursor("Configuration.Dynamic_simulation");
  if (!cursor->get("loadWatchFrequency",&p_loadWatchFrequency)) {
    p_loadWatchFrequency = 1;
  }
  char buf[128];
  cursor = p_config->getCursor("Configuration.Dynamic_simulation.loadWatch");
  gridpack::utility::Configuration::ChildCursors loads;
  if (cursor) cursor->children(loads);
  int i, j, idx, id, len;
  int ncnt = loads.size();
  std::string load, tag, clean_tag;
  gridpack::dynamic_simulation::DSFullBus *bus;
  if (ncnt > 0) p_busIO->header("Monitoring loads:\n");
  for (i=0; i<ncnt; i++) {
    // Parse contents of "load" field to get bus ID and load tag
    loads[i]->get("busID",&id);
    loads[i]->get("loadID",&tag);
    gridpack::utility::StringUtils util;
    clean_tag = util.clean2Char(tag);
    // Find local bus indices for load
    std::vector<int> local_ids = p_network->getLocalBusIndices(id);
    for (j=0; j<local_ids.size(); j++) {
      bus = dynamic_cast<gridpack::dynamic_simulation::DSFullBus*>
        (p_network->getBus(local_ids[j]).get());
      bus->setWatch(clean_tag,true);
    }
    sprintf(buf,"  Bus: %8d Load ID: %2s\n",id,clean_tag.c_str());
    p_busIO->header(buf);
  }
  if (ncnt > 0) {
    p_loadWatch = true;
    sprintf(buf,"Load Watch Frequency: %d\n",p_loadWatchFrequency);
    p_busIO->header(buf);
    openLoadWatchFile();
  }
}

/**
 * Redirect output from standard out
 * @param filename name of file to write results to
 */
void gridpack::dynamic_simulation::DSFullApp::open(const char *filename)
{
  p_busIO->open(filename);
  p_branchIO->setStream(p_busIO->getStream());
}

void gridpack::dynamic_simulation::DSFullApp::close()
{
  p_busIO->close();
  p_branchIO->setStream(p_busIO->getStream());
}

/**
 * Print string. This can be used to direct output to the file opened using
 * the open command
 * @param buf string to be printed
 */
void gridpack::dynamic_simulation::DSFullApp::print(const char *buf)
{
  p_busIO->header(buf);
}

/**
 * Open file containing generator watch results
 */
void gridpack::dynamic_simulation::DSFullApp::openGeneratorWatchFile()
{
  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = p_config->getCursor("Configuration.Dynamic_simulation");
#ifndef USE_GOSS
  std::string filename;
  if (!p_internal_watch_file_name) {
    if (cursor->get("generatorWatchFileName",&filename)) {
      p_generatorIO.reset(new gridpack::serial_io::SerialBusIO<DSFullNetwork>(128,
            p_network));
      p_generatorIO->open(filename.c_str());
    } else {
      p_busIO->header("No Generator Watch File Name Found\n");
      p_generatorWatch = false;
    }
  } else {
    p_generatorIO.reset(new gridpack::serial_io::SerialBusIO<DSFullNetwork>(128,
          p_network));
    p_generatorIO->open(p_gen_watch_file.c_str());
  }
#else
  std::string topic, URI, username, passwd;
  bool ok = true;
  ok = ok && cursor->get("channelTopic",&topic);
  ok = ok && cursor->get("channelURI",&URI);
  ok = ok && cursor->get("username",&username);
  ok = ok && cursor->get("password",&passwd);
  printf("channeltopic %s \n", topic.c_str());
  printf("channelURI %s \n", URI.c_str());
  printf("username %s \n", username.c_str());
  printf("password %s \n", passwd.c_str());
  if (ok) {
    p_generatorIO.reset(new
        gridpack::serial_io::SerialBusIO<DSFullNetwork>(512,
          p_network));
    p_generatorIO->openChannel(topic.c_str(), URI.c_str(),
        username.c_str(),
        passwd.c_str());
  } else {
    p_busIO->header("Unable to open channel\n");
    p_generatorWatch = false;
  }
#endif
}

/**
 * Close file containing generator watch results
 */
void gridpack::dynamic_simulation::DSFullApp::closeGeneratorWatchFile()
{
  if (p_generatorWatch) {
#ifndef USE_GOSS
    p_generatorIO->close();
#else
    p_generatorIO->closeChannel();
#endif
  }
}

/**
 * Open file containing load watch results
 */
void gridpack::dynamic_simulation::DSFullApp::openLoadWatchFile()
{
  gridpack::utility::Configuration::CursorPtr cursor;
  cursor = p_config->getCursor("Configuration.Dynamic_simulation");
#ifndef USE_GOSS
  std::string filename;
  if (cursor->get("loadWatchFileName",&filename)) {
    p_loadIO.reset(new gridpack::serial_io::SerialBusIO<DSFullNetwork>(128,
          p_network));
    p_loadIO->open(filename.c_str());
  } else {
    p_busIO->header("No Load Watch File Name Found\n");
    p_loadWatch = false;
  }
#else
  std::string topic, URI, username, passwd;
  bool ok = true;
  ok = ok && cursor->get("channelTopic",&topic);
  ok = ok && cursor->get("channelURI",&URI);
  ok = ok && cursor->get("username",&username);
  ok = ok && cursor->get("password",&passwd);
  printf("channeltopic %s \n", topic.c_str());
  printf("channelURI %s \n", URI.c_str());
  printf("username %s \n", username.c_str());
  printf("password %s \n", passwd.c_str());
  if (ok) {
    p_loadIO.reset(new
        gridpack::serial_io::SerialBusIO<DSFullNetwork>(512,
          p_network));
    p_loadIO->openChannel(topic.c_str(), URI.c_str(),
        username.c_str(),
        passwd.c_str());
  } else {
    p_busIO->header("Unable to open channel\n");
    p_loadWatch = false;
  }
#endif
}

/**
 * Close file contain load watch results
 */
void gridpack::dynamic_simulation::DSFullApp::closeLoadWatchFile()
{
  if (p_loadWatch) {
#ifndef USE_GOSS
    p_loadIO->close();
#else
    p_loadIO->closeChannel();
#endif
  }
}
