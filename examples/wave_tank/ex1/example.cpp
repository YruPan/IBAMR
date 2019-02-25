// Filename: main.cpp
//
// Copyright (c) 2002-2019, Amneet Bhalla and Nishant Nangia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright notice,
//      this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of New York University nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE

// Config files
#include <IBAMR_config.h>
#include <IBTK_config.h>
#include <SAMRAI_config.h>

// Headers for basic PETSc functions
#include <petscsys.h>

// Headers for basic SAMRAI objects
#include <BergerRigoutsos.h>
#include <CartesianGridGeometry.h>
#include <LoadBalancer.h>
#include <StandardTagAndInitialize.h>

// Headers for application-specific algorithm/data structure objects
#include "ibtk/IndexUtilities.h"
#include <ibamr/AdvDiffPredictorCorrectorHierarchyIntegrator.h>
#include <ibamr/AdvDiffSemiImplicitHierarchyIntegrator.h>
#include <ibamr/ConstraintIBMethod.h>
#include <ibamr/IBExplicitHierarchyIntegrator.h>
#include <ibamr/IBStandardForceGen.h>
#include <ibamr/IBStandardInitializer.h>
#include <ibamr/INSVCStaggeredConservativeHierarchyIntegrator.h>
#include <ibamr/INSVCStaggeredHierarchyIntegrator.h>
#include <ibamr/INSVCStaggeredNonConservativeHierarchyIntegrator.h>
#include <ibamr/RelaxationLSMethod.h>
#include <ibamr/StokesFirstOrderWaveBcCoef.h>
#include <ibamr/StokesSecondOrderWaveBcCoef.h>
#include <ibamr/SurfaceTensionForceFunction.h>
#include <ibamr/WaveDampingStrategy.h>
#include <ibamr/app_namespaces.h>
#include <ibtk/AppInitializer.h>
#include <ibtk/CartGridFunctionSet.h>
#include <ibtk/LData.h>
#include <ibtk/muParserCartGridFunction.h>
#include <ibtk/muParserRobinBcCoefs.h>

// Application
#include "FlowGravityForcing.h"
#include "GravityForcing.h"
#include "LSLocateGasInterface.h"
#include "LSLocateTrapezoidalInterface.h"
#include "RigidBodyKinematics.h"
#include "SetFluidGasSolidDensity.h"
#include "SetFluidGasSolidViscosity.h"
#include "SetLSProperties.h"
#include "TagLSRefinementCells.h"

// Function prototypes
void output_data(Pointer<PatchHierarchy<NDIM> > patch_hierarchy,
                 Pointer<INSVCStaggeredHierarchyIntegrator> navier_stokes_integrator,
                 LDataManager* l_data_manager,
                 const int iteration_num,
                 const double loop_time,
                 const string& data_dump_dirname);

/*******************************************************************************
 * For each run, the input filename and restart information (if needed) must   *
 * be given on the command line.  For non-restarted case, command line is:     *
 *                                                                             *
 *    executable <input file name>                                             *
 *                                                                             *
 * For restarted run, command line is:                                         *
 *                                                                             *
 *    executable <input file name> <restart directory> <restart number>        *
 *                                                                             *
 *******************************************************************************/
bool
run_example(int argc, char* argv[])
{
    // Initialize PETSc, MPI, and SAMRAI.
    PetscInitialize(&argc, &argv, NULL, NULL);
    SAMRAI_MPI::setCommunicator(PETSC_COMM_WORLD);
    SAMRAI_MPI::setCallAbortInSerialInsteadOfExit();
    SAMRAIManager::startup();

    // Increase maximum patch data component indices
    SAMRAIManager::setMaxNumberPatchDataEntries(2500);

    { // cleanup dynamically allocated objects prior to shutdown

        if (NDIM == 3)
        {
            TBOX_ERROR("This example is only implemented for NDIM = 2!");
        }
        // Parse command line options, set some standard options from the input
        // file, initialize the restart database (if this is a restarted run),
        // and enable file logging.
        Pointer<AppInitializer> app_initializer = new AppInitializer(argc, argv, "IB.log");
        Pointer<Database> input_db = app_initializer->getInputDatabase();

        // Whether or not it is from a restarted run
        const bool is_from_restart = app_initializer->isFromRestart();

        // Get various standard options set in the input file.
        const bool dump_viz_data = app_initializer->dumpVizData();
        const int viz_dump_interval = app_initializer->getVizDumpInterval();
        const bool uses_visit = dump_viz_data && !app_initializer->getVisItDataWriter().isNull();

        const bool dump_restart_data = app_initializer->dumpRestartData();
        const int restart_dump_interval = app_initializer->getRestartDumpInterval();
        const string restart_dump_dirname = app_initializer->getRestartDumpDirectory();

        const bool dump_postproc_data = app_initializer->dumpPostProcessingData();
        const int postproc_data_dump_interval = app_initializer->getPostProcessingDataDumpInterval();
        const string postproc_data_dump_dirname = app_initializer->getPostProcessingDataDumpDirectory();
        if (dump_postproc_data && (postproc_data_dump_interval > 0) && !postproc_data_dump_dirname.empty())
        {
            Utilities::recursiveMkdir(postproc_data_dump_dirname);
        }

        const bool dump_timer_data = app_initializer->dumpTimerData();
        const int timer_dump_interval = app_initializer->getTimerDumpInterval();

        // Create major algorithm and data objects that comprise the
        // application.  These objects are configured from the input database
        // and, if this is a restarted run, from the restart database.
        Pointer<INSVCStaggeredHierarchyIntegrator> navier_stokes_integrator;
        const string discretization_form =
            app_initializer->getComponentDatabase("Main")->getString("discretization_form");
        const bool conservative_form = (discretization_form == "CONSERVATIVE");
        if (conservative_form)
        {
            navier_stokes_integrator = new INSVCStaggeredConservativeHierarchyIntegrator(
                "INSVCStaggeredConservativeHierarchyIntegrator",
                app_initializer->getComponentDatabase("INSVCStaggeredConservativeHierarchyIntegrator"));
        }
        else if (!conservative_form)
        {
            navier_stokes_integrator = new INSVCStaggeredNonConservativeHierarchyIntegrator(
                "INSVCStaggeredNonConservativeHierarchyIntegrator",
                app_initializer->getComponentDatabase("INSVCStaggeredNonConservativeHierarchyIntegrator"));
        }
        else
        {
            TBOX_ERROR("Unsupported solver type: " << discretization_form << "\n"
                                                   << "Valid options are: CONSERVATIVE, NON_CONSERVATIVE");
        }

        // Set up the advection diffusion hierarchy integrator
        Pointer<AdvDiffHierarchyIntegrator> adv_diff_integrator;
        const string adv_diff_solver_type = app_initializer->getComponentDatabase("Main")->getStringWithDefault(
            "adv_diff_solver_type", "PREDICTOR_CORRECTOR");
        if (adv_diff_solver_type == "PREDICTOR_CORRECTOR")
        {
            Pointer<AdvectorExplicitPredictorPatchOps> predictor = new AdvectorExplicitPredictorPatchOps(
                "AdvectorExplicitPredictorPatchOps",
                app_initializer->getComponentDatabase("AdvectorExplicitPredictorPatchOps"));
            adv_diff_integrator = new AdvDiffPredictorCorrectorHierarchyIntegrator(
                "AdvDiffPredictorCorrectorHierarchyIntegrator",
                app_initializer->getComponentDatabase("AdvDiffPredictorCorrectorHierarchyIntegrator"),
                predictor);
        }
        else if (adv_diff_solver_type == "SEMI_IMPLICIT")
        {
            adv_diff_integrator = new AdvDiffSemiImplicitHierarchyIntegrator(
                "AdvDiffSemiImplicitHierarchyIntegrator",
                app_initializer->getComponentDatabase("AdvDiffSemiImplicitHierarchyIntegrator"));
        }
        else
        {
            TBOX_ERROR("Unsupported solver type: " << adv_diff_solver_type << "\n"
                                                   << "Valid options are: PREDICTOR_CORRECTOR, SEMI_IMPLICIT");
        }
        navier_stokes_integrator->registerAdvDiffHierarchyIntegrator(adv_diff_integrator);

        const int num_structures = input_db->getIntegerWithDefault("num_structures", 1);
        Pointer<ConstraintIBMethod> ib_method_ops = new ConstraintIBMethod(
            "ConstraintIBMethod", app_initializer->getComponentDatabase("ConstraintIBMethod"), num_structures);
        Pointer<IBHierarchyIntegrator> time_integrator =
            new IBExplicitHierarchyIntegrator("IBHierarchyIntegrator",
                                              app_initializer->getComponentDatabase("IBHierarchyIntegrator"),
                                              ib_method_ops,
                                              navier_stokes_integrator);

        Pointer<CartesianGridGeometry<NDIM> > grid_geometry = new CartesianGridGeometry<NDIM>(
            "CartesianGeometry", app_initializer->getComponentDatabase("CartesianGeometry"));
        Pointer<PatchHierarchy<NDIM> > patch_hierarchy = new PatchHierarchy<NDIM>("PatchHierarchy", grid_geometry);

        Pointer<StandardTagAndInitialize<NDIM> > error_detector =
            new StandardTagAndInitialize<NDIM>("StandardTagAndInitialize",
                                               time_integrator,
                                               app_initializer->getComponentDatabase("StandardTagAndInitialize"));
        Pointer<BergerRigoutsos<NDIM> > box_generator = new BergerRigoutsos<NDIM>();
        Pointer<LoadBalancer<NDIM> > load_balancer =
            new LoadBalancer<NDIM>("LoadBalancer", app_initializer->getComponentDatabase("LoadBalancer"));
        Pointer<GriddingAlgorithm<NDIM> > gridding_algorithm =
            new GriddingAlgorithm<NDIM>("GriddingAlgorithm",
                                        app_initializer->getComponentDatabase("GriddingAlgorithm"),
                                        error_detector,
                                        box_generator,
                                        load_balancer);

        // Configure the IB solver.
        Pointer<IBStandardInitializer> ib_initializer = new IBStandardInitializer(
            "IBStandardInitializer", app_initializer->getComponentDatabase("IBStandardInitializer"));
        ib_method_ops->registerLInitStrategy(ib_initializer);
        Pointer<IBStandardForceGen> ib_force_fcn = new IBStandardForceGen();
        ib_method_ops->registerIBLagrangianForceFunction(ib_force_fcn);

        // Setup level set information
        TrapezoidalInterface trapezoid;
        input_db->getDoubleArray("BL", (trapezoid.BL).data(), NDIM);
        input_db->getDoubleArray("TL", (trapezoid.TL).data(), NDIM);
        input_db->getDoubleArray("TR", (trapezoid.TR).data(), NDIM);
        input_db->getDoubleArray("BR", (trapezoid.BR).data(), NDIM);
        const double fluid_height = input_db->getDouble("GAS_LS_INIT");

        const string& ls_name_solid = "level_set_solid";
        Pointer<CellVariable<NDIM, double> > phi_var_solid = new CellVariable<NDIM, double>(ls_name_solid);
        Pointer<RelaxationLSMethod> level_set_solid_ops =
            new RelaxationLSMethod(ls_name_solid, app_initializer->getComponentDatabase("LevelSet_Solid"));
        LSLocateTrapezoidalInterface* ptr_LSLocateTrapezoidalInterface = new LSLocateTrapezoidalInterface(
            "LSLocateTrapezoidalInterface", adv_diff_integrator, phi_var_solid, &trapezoid);
        level_set_solid_ops->registerInterfaceNeighborhoodLocatingFcn(
            &callLSLocateTrapezoidalInterfaceCallbackFunction, static_cast<void*>(ptr_LSLocateTrapezoidalInterface));

        const string& ls_name_gas = "level_set_gas";
        Pointer<CellVariable<NDIM, double> > phi_var_gas = new CellVariable<NDIM, double>(ls_name_gas);
        Pointer<RelaxationLSMethod> level_set_gas_ops =
            new RelaxationLSMethod(ls_name_gas, app_initializer->getComponentDatabase("LevelSet_Gas"));
        LSLocateGasInterface* ptr_LSLocateGasInterface =
            new LSLocateGasInterface("LSLocateGasInterface", adv_diff_integrator, phi_var_gas, fluid_height);
        level_set_gas_ops->registerInterfaceNeighborhoodLocatingFcn(&callLSLocateGasInterfaceCallbackFunction,
                                                                    static_cast<void*>(ptr_LSLocateGasInterface));

        adv_diff_integrator->registerTransportedQuantity(phi_var_solid);
        adv_diff_integrator->setDiffusionCoefficient(phi_var_solid, 0.0);

        // The body is assumed to be stationary in this case
        // adv_diff_integrator->setAdvectionVelocity(phi_var_solid,
        //                                           navier_stokes_integrator->getAdvectionVelocityVariable());

        adv_diff_integrator->registerTransportedQuantity(phi_var_gas);
        adv_diff_integrator->setDiffusionCoefficient(phi_var_gas, 0.0);
        adv_diff_integrator->setAdvectionVelocity(phi_var_gas,
                                                  navier_stokes_integrator->getAdvectionVelocityVariable());

        // Register the reinitialization functions for the level set variables
        SetLSProperties* ptr_setSetLSProperties =
            new SetLSProperties("SetLSProperties", level_set_solid_ops, level_set_gas_ops);
        adv_diff_integrator->registerResetFunction(
            phi_var_solid, &callSetSolidLSCallbackFunction, static_cast<void*>(ptr_setSetLSProperties));
        adv_diff_integrator->registerResetFunction(
            phi_var_gas, &callSetGasLSCallbackFunction, static_cast<void*>(ptr_setSetLSProperties));

        // Setup the advected and diffused quantities.
        Pointer<Variable<NDIM> > rho_var;
        if (conservative_form)
        {
            rho_var = new SideVariable<NDIM, double>("rho");
        }
        else
        {
            rho_var = new CellVariable<NDIM, double>("rho");
        }

        navier_stokes_integrator->registerMassDensityVariable(rho_var);

        Pointer<CellVariable<NDIM, double> > mu_var = new CellVariable<NDIM, double>("mu");
        navier_stokes_integrator->registerViscosityVariable(mu_var);

        // Array for input into callback function
        const int ls_reinit_interval = input_db->getInteger("LS_REINIT_INTERVAL");
        const double rho_fluid = input_db->getDouble("RHO_F");
        const double rho_solid = input_db->getDoubleWithDefault("RHO_S", std::numeric_limits<double>::quiet_NaN());
        const double rho_gas = input_db->getDouble("RHO_G");
        const int num_solid_interface_cells = input_db->getDouble("NUM_SOLID_INTERFACE_CELLS");
        const int num_gas_interface_cells = input_db->getDouble("NUM_GAS_INTERFACE_CELLS");
        const bool set_rho_solid = input_db->getBool("SET_RHO_S");
        SetFluidGasSolidDensity* ptr_setFluidGasSolidDensity = new SetFluidGasSolidDensity("SetFluidGasSolidDensity",
                                                                                           adv_diff_integrator,
                                                                                           phi_var_solid,
                                                                                           phi_var_gas,
                                                                                           rho_fluid,
                                                                                           rho_gas,
                                                                                           rho_solid,
                                                                                           ls_reinit_interval,
                                                                                           num_solid_interface_cells,
                                                                                           num_gas_interface_cells,
                                                                                           set_rho_solid);
        navier_stokes_integrator->registerResetFluidDensityFcn(&callSetFluidGasSolidDensityCallbackFunction,
                                                               static_cast<void*>(ptr_setFluidGasSolidDensity));

        const double mu_fluid = input_db->getDouble("MU_F");
        const double mu_gas = input_db->getDouble("MU_G");
        const double mu_solid = input_db->getDoubleWithDefault("MU_S", std::numeric_limits<double>::quiet_NaN());
        const bool set_mu_solid = input_db->getBool("SET_MU_S");
        SetFluidGasSolidViscosity* ptr_setFluidGasSolidViscosity =
            new SetFluidGasSolidViscosity("SetFluidGasSolidViscosity",
                                          adv_diff_integrator,
                                          phi_var_solid,
                                          phi_var_gas,
                                          mu_fluid,
                                          mu_gas,
                                          mu_solid,
                                          ls_reinit_interval,
                                          num_solid_interface_cells,
                                          num_gas_interface_cells,
                                          set_mu_solid);
        navier_stokes_integrator->registerResetFluidViscosityFcn(&callSetFluidGasSolidViscosityCallbackFunction,
                                                                 static_cast<void*>(ptr_setFluidGasSolidViscosity));

        // Register callback function for tagging refined cells for level set data
        const double tag_value = input_db->getDouble("LS_TAG_VALUE");
        const double tag_thresh = input_db->getDouble("LS_TAG_ABS_THRESH");
        TagLSRefinementCells ls_tagger;
        ls_tagger.d_ls_gas_var = phi_var_gas;
        ls_tagger.d_tag_value = tag_value;
        ls_tagger.d_tag_abs_thresh = tag_thresh;
        ls_tagger.d_adv_diff_solver = adv_diff_integrator;
        time_integrator->registerApplyGradientDetectorCallback(&callTagLSRefinementCellsCallbackFunction,
                                                               static_cast<void*>(&ls_tagger));

        // Create Eulerian initial condition specification objects.
        if (input_db->keyExists("VelocityInitialConditions"))
        {
            Pointer<CartGridFunction> u_init = new muParserCartGridFunction(
                "u_init", app_initializer->getComponentDatabase("VelocityInitialConditions"), grid_geometry);
            navier_stokes_integrator->registerVelocityInitialConditions(u_init);
        }

        if (input_db->keyExists("PressureInitialConditions"))
        {
            Pointer<CartGridFunction> p_init = new muParserCartGridFunction(
                "p_init", app_initializer->getComponentDatabase("PressureInitialConditions"), grid_geometry);
            navier_stokes_integrator->registerPressureInitialConditions(p_init);
        }

        // Create Eulerian boundary condition specification objects (when necessary).
        const IntVector<NDIM>& periodic_shift = grid_geometry->getPeriodicShift();
        vector<RobinBcCoefStrategy<NDIM>*> u_bc_coefs(NDIM);
        const string& wave_type = input_db->getString("WAVE_TYPE");
        if (periodic_shift.min() > 0)
        {
            for (unsigned int d = 0; d < NDIM; ++d)
            {
                u_bc_coefs[d] = NULL;
            }
        }
        else
        {
            for (unsigned int d = 0; d < NDIM; ++d)
            {
                ostringstream bc_coefs_name_stream;
                bc_coefs_name_stream << "u_bc_coefs_" << d;
                const string bc_coefs_name = bc_coefs_name_stream.str();

                ostringstream bc_coefs_db_name_stream;
                bc_coefs_db_name_stream << "VelocityBcCoefs_" << d;
                const string bc_coefs_db_name = bc_coefs_db_name_stream.str();

                if (wave_type == "FIRST_ORDER_STOKES")
                {
                    u_bc_coefs[d] = new StokesFirstOrderWaveBcCoef(
                        bc_coefs_name, d, app_initializer->getComponentDatabase(bc_coefs_db_name), grid_geometry);
                }
                else if (wave_type == "SECOND_ORDER_STOKES")
                {
                    u_bc_coefs[d] = new StokesSecondOrderWaveBcCoef(
                        bc_coefs_name, d, app_initializer->getComponentDatabase(bc_coefs_db_name), grid_geometry);
                }
                else
                {
                    TBOX_ERROR("Unknown WAVE_TYPE = " << wave_type << " specified in the input file" << std::endl);
                }
            }
            navier_stokes_integrator->registerPhysicalBoundaryConditions(u_bc_coefs);
        }

        // Create a damping zone near the channel outlet to absorb water waves via time-splitting approach.
        // This method uses a time splitting approach and modifies the fluid momentum in the
        // post processing step.
        // Register callback function for tagging refined cells for level set data
        const double x_zone_start = input_db->getDouble("X_ZONE_START");
        const double x_zone_end = input_db->getDouble("X_ZONE_END");
        const double depth = input_db->getDouble("DEPTH");
        const double alpha = input_db->getDouble("ALPHA");
        WaveDampingStrategy wave_damper;
        wave_damper.d_x_zone_start = x_zone_start;
        wave_damper.d_x_zone_end = x_zone_end;
        wave_damper.d_depth = depth;
        wave_damper.d_alpha = alpha;
        wave_damper.d_ins_hier_integrator = navier_stokes_integrator;
        wave_damper.d_adv_diff_hier_integrator = adv_diff_integrator;
        wave_damper.d_phi_var = phi_var_gas;
        time_integrator->registerPostprocessIntegrateHierarchyCallback(&callRelaxationZoneCallbackFunction,
                                                                       static_cast<void*>(&wave_damper));

        RobinBcCoefStrategy<NDIM>* rho_bc_coef = NULL;
        if (!(periodic_shift.min() > 0) && input_db->keyExists("DensityBcCoefs"))
        {
            rho_bc_coef = new muParserRobinBcCoefs(
                "rho_bc_coef", app_initializer->getComponentDatabase("DensityBcCoefs"), grid_geometry);
            navier_stokes_integrator->registerMassDensityBoundaryConditions(rho_bc_coef);
        }

        RobinBcCoefStrategy<NDIM>* mu_bc_coef = NULL;
        if (!(periodic_shift.min() > 0) && input_db->keyExists("ViscosityBcCoefs"))
        {
            mu_bc_coef = new muParserRobinBcCoefs(
                "mu_bc_coef", app_initializer->getComponentDatabase("ViscosityBcCoefs"), grid_geometry);
            navier_stokes_integrator->registerViscosityBoundaryConditions(mu_bc_coef);
        }

        RobinBcCoefStrategy<NDIM>* phi_bc_coef = NULL;
        if (!(periodic_shift.min() > 0) && input_db->keyExists("PhiBcCoefs"))
        {
            phi_bc_coef = new muParserRobinBcCoefs(
                "phi_bc_coef", app_initializer->getComponentDatabase("PhiBcCoefs"), grid_geometry);
        }
        adv_diff_integrator->setPhysicalBcCoef(phi_var_gas, phi_bc_coef);
        adv_diff_integrator->setPhysicalBcCoef(phi_var_solid, phi_bc_coef);

        // LS reinit boundary conditions, which is set to be the same as the BCs
        // for advection
        level_set_solid_ops->registerPhysicalBoundaryCondition(phi_bc_coef);
        level_set_gas_ops->registerPhysicalBoundaryCondition(phi_bc_coef);

        // Initialize objects
        std::vector<double> grav_const(NDIM);
        input_db->getDoubleArray("GRAV_CONST", &grav_const[0], NDIM);
        const string grav_type = input_db->getStringWithDefault("GRAV_TYPE", "FULL");
        Pointer<CartGridFunction> grav_force;
        if (grav_type == "FULL")
        {
            grav_force = new GravityForcing("GravityForcing", navier_stokes_integrator, grav_const);
        }
        else if (grav_type == "FLOW")
        {
            grav_force = new FlowGravityForcing("FlowGravityForcing",
                                                app_initializer->getComponentDatabase("FlowGravityForcing"),
                                                adv_diff_integrator,
                                                phi_var_gas,
                                                grav_const);
        }

        Pointer<SurfaceTensionForceFunction> surface_tension_force =
            new SurfaceTensionForceFunction("SurfaceTensionForceFunction",
                                            app_initializer->getComponentDatabase("SurfaceTensionForceFunction"),
                                            adv_diff_integrator,
                                            phi_var_gas);

        Pointer<CartGridFunctionSet> eul_forces = new CartGridFunctionSet("eulerian_forces");
        eul_forces->addFunction(grav_force);
        eul_forces->addFunction(surface_tension_force);
        time_integrator->registerBodyForceFunction(eul_forces);

        // Set up visualization plot file writers.
        Pointer<VisItDataWriter<NDIM> > visit_data_writer = app_initializer->getVisItDataWriter();
        Pointer<LSiloDataWriter> silo_data_writer = app_initializer->getLSiloDataWriter();
        if (uses_visit)
        {
            ib_initializer->registerLSiloDataWriter(silo_data_writer);
            ib_method_ops->registerLSiloDataWriter(silo_data_writer);
            time_integrator->registerVisItDataWriter(visit_data_writer);
        }

        // Initialize hierarchy configuration and data on all patches.
        time_integrator->initializePatchHierarchy(patch_hierarchy, gridding_algorithm);

        // Create ConstraintIBKinematics objects
        vector<Pointer<ConstraintIBKinematics> > ibkinematics_ops_vec;
        Pointer<ConstraintIBKinematics> ib_kinematics_op;
        // struct_0
        const string& object_name = (NDIM == 2 ? "Trapezoid" : "NA");
        ib_kinematics_op = new RigidBodyKinematics(
            object_name,
            app_initializer->getComponentDatabase("ConstraintIBKinematics")->getDatabase(object_name),
            ib_method_ops->getLDataManager(),
            patch_hierarchy);
        ibkinematics_ops_vec.push_back(ib_kinematics_op);

        // Register ConstraintIBKinematics, physical boundary operators and
        // other things with ConstraintIBMethod.
        ib_method_ops->registerConstraintIBKinematics(ibkinematics_ops_vec);
        const double vol_elem = input_db->getDoubleWithDefault("VOL_ELEM", -1.0);
        if (vol_elem > 0.0) ib_method_ops->setVolumeElement(vol_elem, 0);
        ib_method_ops->setVelocityPhysBdryOp(time_integrator->getVelocityPhysBdryOp());
        ib_method_ops->initializeHierarchyOperatorsandData();

        // Deallocate initialization objects.
        ib_method_ops->freeLInitStrategy();
        ib_initializer.setNull();
        app_initializer.setNull();

        // Print the input database contents to the log file.
        plog << "Input database:\n";
        input_db->printClassData(plog);

        // Write out initial visualization data.
        int iteration_num = time_integrator->getIntegratorStep();
        double loop_time = time_integrator->getIntegratorTime();
        if (dump_viz_data && uses_visit)
        {
            pout << "\n\nWriting visualization files...\n\n";
            time_integrator->setupPlotData();
            visit_data_writer->writePlotData(patch_hierarchy, iteration_num, loop_time);
            silo_data_writer->writePlotData(iteration_num, loop_time);
        }

        // Get the probe points from the input file
        Pointer<Database> probe_db = input_db->getDatabase("ProbePoints");
        const int num_probes = (probe_db->getAllKeys()).getSize();
        std::vector<std::vector<double> > probe_points;
        std::vector<std::ofstream*> probe_streams;
        probe_points.resize(num_probes);
        probe_streams.resize(num_probes);
        for (int i = 0; i < num_probes; ++i)
        {
            std::string probe_name = "probe_" + Utilities::intToString(i);
            probe_points[i].resize(NDIM);
            probe_db->getDoubleArray(probe_name, &probe_points[i][0], NDIM);

            if (!SAMRAI_MPI::getRank())
            {
                if (is_from_restart)
                {
                    probe_streams[i] = new std::ofstream(probe_name.c_str(), std::fstream::app);
                    probe_streams[i]->precision(10);
                }
                else
                {
                    probe_streams[i] = new std::ofstream(probe_name.c_str(), std::fstream::out);

                    *probe_streams[i] << "Printing level set at cell center closest to point (" << probe_points[i][0]
                                      << ", " << probe_points[i][1]
#if (NDIM == 3)
                                      << ", " << probe_points[i][2]
#endif
                                      << ") " << std::endl;
                    probe_streams[i]->precision(10);
                }
            }
        }

        // File to write to for fluid mass data
        ofstream mass_file;
        if (!SAMRAI_MPI::getRank())
        {
            if (is_from_restart)
                mass_file.open("mass_fluid.txt", std::fstream::app);
            else
                mass_file.open("mass_fluid.txt", std::fstream::out);
        }

        // Files to write force data
        ofstream lag_force_file, grav_force_file;
        if (!SAMRAI_MPI::getRank())
        {
            if (is_from_restart)
                lag_force_file.open("lag_force.txt", std::fstream::app);
            else
                lag_force_file.open("lag_force.txt", std::fstream::out);

            if (is_from_restart)
                grav_force_file.open("grav_force.txt", std::fstream::app);
            else
                grav_force_file.open("grav_force.txt", std::fstream::out);
        }

        // Main time step loop.
        double loop_time_end = time_integrator->getEndTime();
        double dt = 0.0;
        while (!MathUtilities<double>::equalEps(loop_time, loop_time_end) && time_integrator->stepsRemaining())
        {
            iteration_num = time_integrator->getIntegratorStep();
            loop_time = time_integrator->getIntegratorTime();

            pout << "\n";
            pout << "+++++++++++++++++++++++++++++++++++++++++++++++++++\n";
            pout << "At beginning of timestep # " << iteration_num << "\n";
            pout << "Simulation time is " << loop_time << "\n";

            dt = time_integrator->getMaximumTimeStepSize();
            pout << "Advancing hierarchy with timestep size dt = " << dt << "\n";
            time_integrator->advanceHierarchy(dt);
            loop_time += dt;

            pout << "\n";
            pout << "At end       of timestep # " << iteration_num << "\n";
            pout << "Simulation time is " << loop_time << "\n";
            pout << "+++++++++++++++++++++++++++++++++++++++++++++++++++\n";
            pout << "\n";

            // Compute the fluid mass in the domain from interpolated density
            const int rho_ins_idx = navier_stokes_integrator->getLinearOperatorRhoPatchDataIndex();
#if !defined(NDEBUG)
            TBOX_ASSERT(rho_ins_idx >= 0);
#endif
            const int coarsest_ln = 0;
            const int finest_ln = patch_hierarchy->getFinestLevelNumber();
            HierarchySideDataOpsReal<NDIM, double> hier_sc_data_ops(patch_hierarchy, coarsest_ln, finest_ln);
            HierarchyMathOps hier_math_ops("HierarchyMathOps", patch_hierarchy);
            hier_math_ops.setPatchHierarchy(patch_hierarchy);
            hier_math_ops.resetLevels(coarsest_ln, finest_ln);
            const int wgt_sc_idx = hier_math_ops.getSideWeightPatchDescriptorIndex();
            const double mass_fluid = hier_sc_data_ops.integral(rho_ins_idx, wgt_sc_idx);

            VariableDatabase<NDIM>* var_db = VariableDatabase<NDIM>::getDatabase();

            // Write to file
            if (!SAMRAI_MPI::getRank())
            {
                mass_file << std::setprecision(13) << loop_time << '\t' << mass_fluid << std::endl;
            }

            // Get the values of the Lagrange multiplier to compute forces
            const int struct_id = 0;
            const double dV = (ib_method_ops->getVolumeElement())[struct_id];
            IBTK::Vector3d lm_force = IBTK::Vector3d::Zero();
            IBTK::Vector3d g_force = IBTK::Vector3d::Zero();
            std::vector<Pointer<LData> > lag_force = ib_method_ops->getFullLagrangeMultiplierForce();
            std::vector<Pointer<LData> > lag_rho = ib_method_ops->getInterpolatedLagrangianDensity();
            LDataManager* l_data_manager = ib_method_ops->getLDataManager();
            for (int ln = coarsest_ln; ln <= finest_ln; ++ln)
            {
                if (!l_data_manager->levelContainsLagrangianData(ln)) continue;
                boost::multi_array_ref<double, 2>& F_data = *lag_force[ln]->getLocalFormVecArray();
                boost::multi_array_ref<double, 2>& R_data = *lag_rho[ln]->getLocalFormVecArray();
                const Pointer<LMesh> mesh = l_data_manager->getLMesh(ln);
                const std::vector<LNode*>& local_nodes = mesh->getLocalNodes();
                const std::vector<int> structIDs = l_data_manager->getLagrangianStructureIDs(ln);

                // Dealing specifically with a single structure
                std::pair<int, int> lag_idx_range =
                    l_data_manager->getLagrangianStructureIndexRange(structIDs[struct_id], ln);
                for (std::vector<LNode*>::const_iterator cit = local_nodes.begin(); cit != local_nodes.end(); ++cit)
                {
                    const LNode* const node_idx = *cit;
                    const int lag_idx = node_idx->getLagrangianIndex();
                    if (lag_idx_range.first <= lag_idx && lag_idx < lag_idx_range.second)
                    {
                        const int local_idx = node_idx->getLocalPETScIndex();
                        const double* const F = &F_data[local_idx][0];
                        const double* const R = &R_data[local_idx][0];

                        for (int d = 0; d < NDIM; ++d) lm_force[d] += F[d] * dV;
                        for (int d = 0; d < NDIM; ++d) g_force[d] += grav_const[d] * R[d] * dV;
                    }
                }
                lag_force[ln]->restoreArrays();
            }
            SAMRAI_MPI::sumReduction(lm_force.data(), 3);
            SAMRAI_MPI::sumReduction(g_force.data(), 3);
            if (!SAMRAI_MPI::getRank())
            {
                lag_force_file << std::setprecision(8) << loop_time << '\t' << lm_force(0) << '\t' << lm_force(1)
                               << '\t' << lm_force(2) << std::endl;

                grav_force_file << std::setprecision(8) << loop_time << '\t' << g_force(0) << '\t' << g_force(1) << '\t'
                                << g_force(2) << std::endl;
            }

            // Print out the level set values at probe locations
            // Note that it will print the nearest cell center
            // Max reduction over ls_val array to ensure that only processor 0 prints
            int phi_idx = var_db->mapVariableAndContextToIndex(phi_var_gas, adv_diff_integrator->getCurrentContext());
            std::vector<double> ls_val(num_probes);
            for (int i = 0; i < num_probes; ++i)
            {
                ls_val[i] = -std::numeric_limits<double>::max();
                bool found_point_in_patch = false;
                for (int ln = finest_ln; ln >= coarsest_ln; --ln)
                {
                    // Get the cell index for this point
                    Pointer<PatchLevel<NDIM> > level = patch_hierarchy->getPatchLevel(ln);
                    const CellIndex<NDIM> cell_idx =
                        IndexUtilities::getCellIndex(&probe_points[i][0], level->getGridGeometry(), level->getRatio());
                    for (PatchLevel<NDIM>::Iterator p(level); p; p++)
                    {
                        Pointer<Patch<NDIM> > patch = level->getPatch(p());
                        const Box<NDIM>& patch_box = patch->getBox();
                        const bool contains_probe = patch_box.contains(cell_idx);
                        if (!contains_probe) continue;

                        // Get the level set value at this particular cell and print to stream
                        Pointer<CellData<NDIM, double> > phi_data = patch->getPatchData(phi_idx);
                        ls_val[i] = (*phi_data)(cell_idx);
                        found_point_in_patch = true;
                        if (found_point_in_patch) break;
                    }
                    if (found_point_in_patch) break;
                }
            }
            SAMRAI_MPI::maxReduction(&ls_val[0], num_probes);
            if (!SAMRAI_MPI::getRank())
            {
                for (int i = 0; i < num_probes; ++i)
                {
                    *probe_streams[i] << loop_time << '\t' << ls_val[i] << std::endl;
                }
            }

            // At specified intervals, write visualization and restart files,
            // print out timer data, and store hierarchy data for post
            // processing.
            iteration_num += 1;
            const bool last_step = !time_integrator->stepsRemaining();
            if (dump_viz_data && uses_visit && (iteration_num % viz_dump_interval == 0 || last_step))
            {
                pout << "\nWriting visualization files...\n\n";
                time_integrator->setupPlotData();
                visit_data_writer->writePlotData(patch_hierarchy, iteration_num, loop_time);
                silo_data_writer->writePlotData(iteration_num, loop_time);
            }
            if (dump_restart_data && (iteration_num % restart_dump_interval == 0 || last_step))
            {
                pout << "\nWriting restart files...\n\n";
                RestartManager::getManager()->writeRestartFile(restart_dump_dirname, iteration_num);
            }
            if (dump_timer_data && (iteration_num % timer_dump_interval == 0 || last_step))
            {
                pout << "\nWriting timer data...\n\n";
                TimerManager::getManager()->print(plog);
            }
            if (dump_postproc_data && (iteration_num % postproc_data_dump_interval == 0 || last_step))
            {
                output_data(patch_hierarchy,
                            navier_stokes_integrator,
                            ib_method_ops->getLDataManager(),
                            iteration_num,
                            loop_time,
                            postproc_data_dump_dirname);
            }
        }

        // Close file
        if (!SAMRAI_MPI::getRank()) mass_file.close();
        if (!SAMRAI_MPI::getRank()) lag_force_file.close();
        if (!SAMRAI_MPI::getRank()) grav_force_file.close();

        // Cleanup Eulerian boundary condition specification objects (when
        // necessary).
        for (unsigned int d = 0; d < NDIM; ++d) delete u_bc_coefs[d];
        delete ptr_LSLocateTrapezoidalInterface;
        delete ptr_LSLocateGasInterface;
        delete ptr_setFluidGasSolidDensity;
        delete ptr_setFluidGasSolidViscosity;
        delete ptr_setSetLSProperties;
        delete rho_bc_coef;
        delete mu_bc_coef;
        delete phi_bc_coef;
        for (int i = 0; i < num_probes; ++i) delete probe_streams[i];

    } // cleanup dynamically allocated objects prior to shutdown

    SAMRAIManager::shutdown();
    PetscFinalize();
    return true;
} // run_example

void
output_data(Pointer<PatchHierarchy<NDIM> > patch_hierarchy,
            Pointer<INSVCStaggeredHierarchyIntegrator> navier_stokes_integrator,
            LDataManager* l_data_manager,
            const int iteration_num,
            const double loop_time,
            const string& data_dump_dirname)
{
    plog << "writing hierarchy data at iteration " << iteration_num << " to disk" << endl;
    plog << "simulation time is " << loop_time << endl;

    // Write Cartesian data.
    string file_name = data_dump_dirname + "/" + "hier_data.";
    char temp_buf[128];
    sprintf(temp_buf, "%05d.samrai.%05d", iteration_num, SAMRAI_MPI::getRank());
    file_name += temp_buf;
    Pointer<HDFDatabase> hier_db = new HDFDatabase("hier_db");
    hier_db->create(file_name);
    VariableDatabase<NDIM>* var_db = VariableDatabase<NDIM>::getDatabase();
    ComponentSelector hier_data;
    hier_data.setFlag(var_db->mapVariableAndContextToIndex(navier_stokes_integrator->getVelocityVariable(),
                                                           navier_stokes_integrator->getCurrentContext()));
    hier_data.setFlag(var_db->mapVariableAndContextToIndex(navier_stokes_integrator->getPressureVariable(),
                                                           navier_stokes_integrator->getCurrentContext()));
    patch_hierarchy->putToDatabase(hier_db->putDatabase("PatchHierarchy"), hier_data);
    hier_db->putDouble("loop_time", loop_time);
    hier_db->putInteger("iteration_num", iteration_num);
    hier_db->close();

    // Write Lagrangian data.
    const int finest_hier_level = patch_hierarchy->getFinestLevelNumber();
    Pointer<LData> X_data = l_data_manager->getLData("X", finest_hier_level);
    Vec X_petsc_vec = X_data->getVec();
    Vec X_lag_vec;
    VecDuplicate(X_petsc_vec, &X_lag_vec);
    l_data_manager->scatterPETScToLagrangian(X_petsc_vec, X_lag_vec, finest_hier_level);
    file_name = data_dump_dirname + "/" + "X.";
    sprintf(temp_buf, "%05d", iteration_num);
    file_name += temp_buf;
    PetscViewer viewer;
    PetscViewerASCIIOpen(PETSC_COMM_WORLD, file_name.c_str(), &viewer);
    VecView(X_lag_vec, viewer);
    PetscViewerDestroy(&viewer);
    VecDestroy(&X_lag_vec);
    return;
} // output_data