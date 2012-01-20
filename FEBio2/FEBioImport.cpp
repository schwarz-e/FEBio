// FEBioImport.cpp: implementation of the FEFEBioImport class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "FEBioImport.h"
#include "FEBioLib/FEPeriodicBoundary.h"
#include "FEBioLib/FESurfaceConstraint.h"
#include "NumCore/ConjGradIterSolver.h"
#include "FEBioLib/SuperLUSolver.h"
#include "FESolidSolver.h"
#include "FEHeatSolver.h"
#include "FELinearSolidSolver.h"
#include "FEBioLib/log.h"
#include "LSDYNAPlotFile.h"
#include "FEBioPlotFile.h"
#include "ut4.h"
#include "FEBioLib/FEDiscreteMaterial.h"
#include "FEBioLib/FEUncoupledMaterial.h"
#include "FEFacet2FacetSliding.h"
#include "FESlidingInterface.h"
#include "FESlidingInterface2.h"
#include "FESlidingInterface3.h"
#include "FEBioLib/FETiedInterface.h"
#include "FERigidWallInterface.h"
#include "FEBiphasicSolver.h"
#include "FEBiphasicSoluteSolver.h"
#include "FECoupledHeatSolidSolver.h"
#include "FEBioLib/FEPressureLoad.h"
#include "FEBioLib/FETractionLoad.h"
#include "FEBioLib/FEHeatFlux.h"
#include "FEFluidFlux.h"
#include "FEPoroTraction.h"
#include "FESoluteFlux.h"
#include "plugin.h"
#include <string.h>
#include "FEUDGHexDomain.h"
#include "FERigidSolidDomain.h"
#include "FEBiphasicDomain.h"
#include "FEBiphasicSoluteDomain.h"
#include "FEHeatSolidDomain.h"
#include "FELinearSolidDomain.h"
#include "FEElasticTrussDomain.h"
#include "FEElasticShellDomain.h"
#include "FEDiscreteSpringDomain.h"
#include "FE3FieldElasticSolidDomain.h"
#include "FEBioLib/FEConstBodyForce.h"
#include "FEBioLib/FEPointBodyForce.h"
#include "FEBioLib/FEHeatTransferMaterial.h"
using namespace NumCore;

//-----------------------------------------------------------------------------
FEM* FEBioFileSection::GetFEM() { return m_pim->GetFEM(); }
FEAnalysisStep* FEBioFileSection::GetStep() { return dynamic_cast<FEAnalysisStep*>(m_pim->GetStep()); }

//-----------------------------------------------------------------------------
FEBioFileSectionMap::~FEBioFileSectionMap()
{
	// clear the map
	FEBioFileSectionMap::iterator is;
	for (is = begin(); is != end(); ++is)
	{
		FEBioFileSection* ps = is->second; delete ps;
	}
	clear();
}

//=============================================================================
// FUNCTION : FEFEBioImport::Load
//  Imports an XML input file.
//  The actual file is parsed using the XMLReader class.
//
bool FEFEBioImport::Load(FEM& fem, const char* szfile)
{
	// store a copy of the file name
	fem.SetInputFilename(szfile);

	// Open the XML file
	XMLReader xml;
	if (xml.Open(szfile) == false) return errf("FATAL ERROR: Failed opening input file %s\n\n", szfile);

	// keep a pointer to the fem object
	m_pfem = &fem;

	// Create one step
	if (fem.Steps() == 0)
	{
		FEAnalysisStep* pstep = new FEAnalysisStep(fem);
		fem.AddStep(pstep);
		fem.m_nStep = 0;
		fem.m_pStep = pstep;
	}
	assert(fem.m_pStep);

	// get a pointer to the first step
	// since we assume that the FEM object will always have
	// at least one step defined
	int nsteps = fem.Steps();
	assert(nsteps > 0);
	m_pStep = fem.GetStep(nsteps-1);
	m_nsteps = 0; // reset step section counter
	m_nversion = -1;

	// default element type
	m_ntet4 = ET_TET4;
	m_nhex8 = FE_HEX;
	m_nut4  = FE_TETG1;

	// 3-field formulation on by default
	m_b3field = true;

	// Find the root element
	XMLTag tag;
	try
	{
		if (xml.FindTag("febio_spec", tag) == false) return errf("FATAL ERROR: febio_spec tag was not found. This is not a valid input file.\n\n");
	}
	catch (...)
	{
		clog.printf("An error occured while finding the febio_spec tag.\nIs this a valid FEBio input file?\n\n");
		return false;
	}

	// loop over all child tags
	try
	{
		// get the version number
		ParseVersion(tag);
		if ((m_nversion != 0x0100) && 
			(m_nversion != 0x0101) &&
			(m_nversion != 0x0200)) throw InvalidVersion();

		// define the file structure
		FEBioFileSectionMap map;
		map["Import"     ] = new FEBioImportSection     (this);
		map["Module"     ] = new FEBioModuleSection     (this);
		map["Control"    ] = new FEBioControlSection    (this);
		map["Material"   ] = new FEBioMaterialSection   (this);
		map["Geometry"   ] = new FEBioGeometrySection   (this);
		map["Boundary"   ] = new FEBioBoundarySection   (this);
		map["Initial"    ] = new FEBioInitialSection    (this);
		map["LoadData"   ] = new FEBioLoadSection       (this);
		map["Globals"    ] = new FEBioGlobalsSection    (this);
		map["Output"     ] = new FEBioOutputSection     (this);
		map["Constraints"] = new FEBioConstraintsSection(this);
		map["Step"       ] = new FEBioStepSection       (this);

		// version 2.0 only!
		if (m_nversion >= 0x0200)
		{
			map["Contact"] = new FEBioContactSection(this);
		}

		// parse the file
		++tag;
		do
		{
			// try to find a section parser
			FEBioFileSectionMap::iterator is = map.find(tag.Name());

			// make sure we found a section reader
			if (is == map.end()) throw XMLReader::InvalidTag(tag);

			// see if the file has the "from" attribute (for version 2.0 and up)
			if (m_nversion >= 0x0200)
			{
				const char* szinc = tag.AttributeValue("from", true);
				if (szinc)
				{
					// make sure this is a leaf
					if (tag.isleaf() == false) return errf("FATAL ERROR: included sections may not have child sections.\n\n");

					// read this section from an included file.
					XMLReader xml2;
					if (xml2.Open(szinc) == false) return errf("FATAL ERROR: failed opening input file %s\n\n", szinc);

					// find the febio_spec tag
					XMLTag tag2;
					if (xml2.FindTag("febio_spec", tag2) == false) return errf("FATAL ERROR: febio_spec tag was not found. This is not a valid input file.\n\n");

					// find the section we are looking for
					if (xml2.FindTag(tag.Name(), tag2) == false) return errf("FATAL ERROR: Couldn't find %s section in file %s.\n\n", tag.Name(), szinc);

					// parse the section
					is->second->Parse(tag2);
				}
				else is->second->Parse(tag);
			}
			else
			{
				// parse the section
				is->second->Parse(tag);
			}

			// go to the next tag
			++tag;
		}
		while (!tag.isend());
	}
	// --- XML Reader Exceptions ---
	catch (XMLReader::XMLSyntaxError)
	{
		clog.printf("FATAL ERROR: Syntax error (line %d)\n", xml.GetCurrentLine());
		return false;
	}
	catch (XMLReader::InvalidAttributeValue e)
	{
		const char* szt = e.tag.m_sztag;
		const char* sza = e.szatt;
		const char* szv = e.szval;
		int l = e.tag.m_nstart_line;
		clog.printf("FATAL ERROR: invalid value \"%s\" for attribute \"%s.%s\" (line %d)\n", szv, szt, sza, l);
		return false;
	}
	catch (XMLReader::InvalidValue e)
	{
		clog.printf("FATAL ERROR: the value for tag \"%s\" is invalid (line %d)\n", e.tag.m_sztag, e.tag.m_nstart_line);
		return false;
	}
	catch (XMLReader::MissingAttribute e)
	{
		clog.printf("FATAL ERROR: Missing attribute \"%s\" of tag \"%s\" (line %d)\n", e.szatt, e.tag.m_sztag, e.tag.m_nstart_line);
		return false;
	}
	catch (XMLReader::UnmatchedEndTag e)
	{
		const char* sz = e.tag.m_szroot[e.tag.m_nlevel];
		clog.printf("FATAL ERROR: Unmatched end tag for \"%s\" (line %d)\n", sz, e.tag.m_nstart_line);
		return false;
	}
	// --- FEBio Exceptions ---
	catch (InvalidVersion)
	{
		clog.printbox("FATAL ERROR", "Invalid version for FEBio specification.");
		return false;
	}
	catch (InvalidMaterial e)
	{
		clog.printbox("FATAL ERROR:", "Element %d has an invalid material type.", e.m_nel);
		return false;
	}
	catch (XMLReader::InvalidTag e)
	{
		clog.printf("FATAL ERROR: unrecognized tag \"%s\" (line %d)\n", e.tag.m_sztag, e.tag.m_nstart_line);
		return false;
	}
	catch (InvalidDomainType)	
	{
		clog.printf("Fatal Error: Invalid domain type\n");
		return false;
	}
	catch (FailedCreatingDomain)
	{
		clog.printf("Fatal Error: Failed creating domain\n");
		return false;
	}
	catch (InvalidElementType)
	{
		clog.printf("Fatal Error: Invalid element type\n");
		return false;
	}
	catch (FailedLoadingPlugin e)
	{
		clog.printf("Fatal Error: failed loading plugin %s\n", e.FileName());
		return false;
	}
	catch (UnknownDataField e)
	{
		clog.printf("Fatal Error: \"%s\" is not a valid field variable name (line %d)\n", e.m_szdata, xml.GetCurrentLine()-1);
		return false;
	}
	catch (DuplicateMaterialSection)
	{
		clog.printf("Fatal Error: Material section has already been defined (line %d).\n", xml.GetCurrentLine()-1);
		return false;
	}
	// --- Unknown exceptions ---
	catch (...)
	{
		clog.printf("FATAL ERROR: unrecoverable error (line %d)\n", xml.GetCurrentLine());
		return false;
	}

	// close the XML file
	xml.Close();

	// we're done!
	return true;
}

//-----------------------------------------------------------------------------
//! This function parses the febio_spec tag for the version number
void FEFEBioImport::ParseVersion(XMLTag &tag)
{
	const char* szv = tag.AttributeValue("version");
	assert(szv);
	int n1, n2;
	int nr = sscanf(szv, "%d.%d", &n1, &n2);
	if (nr != 2) throw InvalidVersion();
	if ((n1 < 1) || (n1 > 0xFF)) throw InvalidVersion();
	if ((n2 < 0) || (n2 > 0xFF)) throw InvalidVersion();
	m_nversion = (n1 << 8) + n2;
}

//-----------------------------------------------------------------------------
//! This function parese a parameter list
bool FEFEBioImport::ReadParameter(XMLTag& tag, FEParameterList& pl)
{
	// see if we can find this parameter
	FEParam* pp = pl.Find(tag.Name());
	if (pp)
	{
		switch (pp->m_itype)
		{
		case FE_PARAM_DOUBLE : tag.value(pp->value<double>() ); break;
		case FE_PARAM_INT    : tag.value(pp->value<int   >() ); break;
		case FE_PARAM_BOOL   : tag.value(pp->value<bool  >() ); break;
		case FE_PARAM_VEC3D  : tag.value(pp->value<vec3d >() ); break;
		case FE_PARAM_STRING : tag.value(pp->cvalue() ); break;
		case FE_PARAM_INTV   : tag.value(pp->pvalue<int   >(), pp->m_ndim); break;
		case FE_PARAM_DOUBLEV: tag.value(pp->pvalue<double>(), pp->m_ndim); break;
		default:
			assert(false);
			return false;
		}

		int nattr = tag.m_natt;
		for (int i=0; i<nattr; ++i)
		{
			const char* szat = tag.m_szatt[i];
			if (strcmp(szat, "lc") == 0)
			{
				int lc = atoi(tag.m_szatv[i]);
				if (lc < 0) throw XMLReader::InvalidAttributeValue(tag, szat, tag.m_szatv[i]);
				pp->m_nlc = lc;
				switch (pp->m_itype)
				{
				case FE_PARAM_DOUBLE: pp->m_scl = pp->value<double>(); break;
				}
			}
			else clog.printf("WARNING: attribute \"%s\" of parameter \"%s\" ignored (line %d)\n", szat, tag.Name(), tag.m_ncurrent_line-1);
		}

		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
void FEFEBioImport::ReadList(XMLTag& tag, vector<int>& l)
{
	// make sure the list is empty
	l.clear();

	// get a pointer to the value
	const char* sz = tag.szvalue();

	// parse the string
	const char* ch;
	do
	{
		int n0, n1, nn;
		int nread = sscanf(sz, "%d:%d:%d", &n0, &n1, &nn);
		switch (nread)
		{
		case 1:
			n1 = n0;
			nn = 1;
			break;
		case 2:
			nn = 1;
			break;
		}

		for (int i=n0; i<=n1; i += nn) l.push_back(i);

		ch = strchr(sz, ',');
		if (ch) sz = ch+1;
	}
	while (ch != 0);
}

//=============================================================================
//
//                     I M P O R T   S E C T I O N
//
//=============================================================================

//! This function parses the Import section which allows a user to load custom
//! dll's. 
void FEBioImportSection::Parse(XMLTag &tag)
{
	const char* szfile = tag.szvalue();
	if (LoadPlugin(szfile) == false) throw FEFEBioImport::FailedLoadingPlugin(szfile);
	clog.printf("Plugin \"%s\" loaded successfully\n", szfile);
}

//=============================================================================
//
//                     M O D U L E   S E C T I O N
//
//=============================================================================
//
//! This function parses the Module section.
//! The Module defines the type of problem the user wants to solve (solid, heat, ...)
//!
void FEBioModuleSection::Parse(XMLTag &tag)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pstep = GetStep();

	// get the type attribute
	const char* szt = tag.AttributeValue("type");

	assert(pstep && (pstep->m_psolver == 0));

	if      (strcmp(szt, "solid"       ) == 0) pstep->m_nModule = FE_SOLID;
	else if (strcmp(szt, "linear solid") == 0) pstep->m_nModule = FE_LINEAR_SOLID; 
	else if (strcmp(szt, "poro"        ) == 0) pstep->m_nModule = FE_BIPHASIC;		// obsolete in 2.0
	else if (strcmp(szt, "biphasic"    ) == 0) pstep->m_nModule = FE_BIPHASIC;
	else if (strcmp(szt, "solute"      ) == 0) pstep->m_nModule = FE_POROSOLUTE;
	else if (strcmp(szt, "heat"        ) == 0) pstep->m_nModule = FE_HEAT;
	else if (strcmp(szt, "heat-solid"  ) == 0) pstep->m_nModule = FE_HEAT_SOLID;
	else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
}

//=============================================================================
//
//                     C O N T R O L   S E C T I O N
//
//=============================================================================

FESolver* FEBioControlSection::BuildSolver(int nmod, FEM& fem)
{
	switch (nmod)
	{
	case FE_SOLID       : return new FESolidSolver(fem);
	case FE_BIPHASIC : return new FEBiphasicSolver(fem);
	case FE_POROSOLUTE  : return new FEBiphasicSoluteSolver(fem);
	case FE_HEAT        : return new FEHeatSolver(fem);
	case FE_LINEAR_SOLID: return new FELinearSolidSolver(fem);
	case FE_HEAT_SOLID  : return new FECoupledHeatSolidSolver(fem);
	default:
		assert(false);
		return 0;
	}
}

void FEBioControlSection::Parse(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pstep = GetStep();

	// make sure we have a solver defined
	if (pstep->m_psolver == 0) pstep->m_psolver = BuildSolver(pstep->m_nModule, fem);

	++tag;
	do
	{
		// first parse common control parameters
		if (ParseCommonParams(tag) == false)
		{
			// next, parse solver specific control parameters
			if (dynamic_cast<FESolidSolver*>(pstep->m_psolver))
			{
				if (ParseSolidParams(tag) == false)
				{
					if (dynamic_cast<FEBiphasicSolver*>(pstep->m_psolver))
					{
						if (ParsePoroParams(tag) == false)
						{
							if (dynamic_cast<FEBiphasicSoluteSolver*>(pstep->m_psolver))
							{
								if (ParseSoluteParams(tag) == false) throw XMLReader::InvalidTag(tag);
							}
							else throw XMLReader::InvalidTag(tag);
						}
					}
					else throw XMLReader::InvalidTag(tag);
				}
			}
			else if (dynamic_cast<FELinearSolidSolver*>(pstep->m_psolver))
			{
				FELinearSolidSolver* ps = dynamic_cast<FELinearSolidSolver*>(pstep->m_psolver);
				if (tag == "dtol") tag.value(ps->m_Dtol);
			}
			else throw XMLReader::InvalidTag(tag);
		}

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// Parse parameters specific to solid solver
bool FEBioControlSection::ParseSolidParams(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pstep = GetStep();

	FESolidSolver* ps = dynamic_cast<FESolidSolver*>(pstep->m_psolver);
	assert(ps);

	if      (tag == "dtol"        ) tag.value(ps->m_Dtol);
	else if (tag == "etol"        ) tag.value(ps->m_Etol);
	else if (tag == "rtol"        ) tag.value(ps->m_Rtol);
	else if (tag == "min_residual") tag.value(ps->m_Rmin);
	else return false;

	return true;
}

//-----------------------------------------------------------------------------
// Parse parameters specific for the poro-elastic solver
bool FEBioControlSection::ParsePoroParams(XMLTag &tag)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pstep = GetStep();

	FEBiphasicSolver* pps = dynamic_cast<FEBiphasicSolver*>(pstep->m_psolver);
	assert(pps);

	if      (tag == "ptol"              ) tag.value(pps->m_Ptol);
	else if (tag == "symmetric_biphasic") tag.value(fem.m_bsym_poro);
	else return false;

	return true;
}

//-----------------------------------------------------------------------------
// Parse parameters specific for the poro-elastic solver
bool FEBioControlSection::ParseSoluteParams(XMLTag &tag)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pstep = GetStep();

	FEBiphasicSoluteSolver* pps = dynamic_cast<FEBiphasicSoluteSolver*>(pstep->m_psolver);
	assert(pps);

	if (tag == "ctol") tag.value(pps->m_Ctol);
	else return false;

	return true;
}

//-----------------------------------------------------------------------------
// Parse control parameters common to all solvers/modules
bool FEBioControlSection::ParseCommonParams(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pstep = GetStep();
	char sztitle[256];

	if      (tag == "title"             ) { tag.value(sztitle); fem.SetTitle(sztitle); }
	else if (tag == "time_steps"        ) tag.value(pstep->m_ntime);
	else if (tag == "final_time"        ) tag.value(pstep->m_final_time);
	else if (tag == "step_size"         ) { tag.value(pstep->m_dt0); pstep->m_dt = pstep->m_dt0; }
	else if (tag == "lstol"             ) tag.value(pstep->m_psolver->m_bfgs.m_LStol);
	else if (tag == "lsmin"             ) tag.value(pstep->m_psolver->m_bfgs.m_LSmin);
	else if (tag == "lsiter"            ) tag.value(pstep->m_psolver->m_bfgs.m_LSiter);
	else if (tag == "max_refs"          ) tag.value(pstep->m_psolver->m_bfgs.m_maxref);
	else if (tag == "max_ups"           ) tag.value(pstep->m_psolver->m_bfgs.m_maxups);
	else if (tag == "cmax"              ) tag.value(pstep->m_psolver->m_bfgs.m_cmax);
	else if (tag == "optimize_bw"       ) tag.value(fem.m_bwopt);
	else if (tag == "pressure_stiffness") tag.value(pstep->m_istiffpr);
	else if (tag == "hourglass"         ) tag.value(pstep->m_hg);
	else if (tag == "plane_strain"      )
	{
		int bc = 2;
		const char* szt = tag.AttributeValue("bc", true);
		if (szt)
		{
			if      (strcmp(szt, "x") == 0) bc = 0;
			else if (strcmp(szt, "y") == 0) bc = 1;
			else if (strcmp(szt, "z") == 0) bc = 2;
			else throw XMLReader::InvalidAttributeValue(tag, "bc", szt);
		}
		bool b = false;
		tag.value(b);
		if (b) fem.m_nplane_strain = bc; else fem.m_nplane_strain = -1;
	}
	else if (tag == "analysis")
	{
		const char* szt = tag.AttributeValue("type");
		if      (strcmp(szt, "static" ) == 0) pstep->m_nanalysis = FE_STATIC;
		else if (strcmp(szt, "dynamic") == 0) pstep->m_nanalysis = FE_DYNAMIC;
		else if (strcmp(szt, "steady-state") == 0) pstep->m_nanalysis = FE_STEADY_STATE;
		else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
	}
	else if (tag == "restart" )
	{
		const char* szf = tag.AttributeValue("file", true);
		if (szf) fem.SetDumpFilename(szf);
		tag.value(pstep->m_bDump);
	}
	else if (tag == "time_stepper")
	{
		pstep->m_bautostep = true;
		++tag;
		do
		{
			if      (tag == "max_retries") tag.value(pstep->m_maxretries);
			else if (tag == "opt_iter"   ) tag.value(pstep->m_iteopt);
			else if (tag == "dtmin"      ) tag.value(pstep->m_dtmin);
			else if (tag == "dtmax"      )
			{
				tag.value(pstep->m_dtmax);
				const char* sz = tag.AttributeValue("lc", true);
				if (sz) sscanf(sz,"%d", &pstep->m_nmplc);
			}
			else if (tag == "aggressiveness") tag.value(pstep->m_naggr);
			else throw XMLReader::InvalidTag(tag);

			++tag;
		}
		while (!tag.isend());
	}
	else if (tag == "plot_level")
	{
		char szval[256];
		tag.value(szval);
		if		(strcmp(szval, "PLOT_DEFAULT"    ) == 0) {} // don't change the plot level
		else if (strcmp(szval, "PLOT_NEVER"      ) == 0) pstep->SetPlotLevel(FE_PLOT_NEVER);
		else if (strcmp(szval, "PLOT_MAJOR_ITRS" ) == 0) pstep->SetPlotLevel(FE_PLOT_MAJOR_ITRS);
		else if (strcmp(szval, "PLOT_MINOR_ITRS" ) == 0) pstep->SetPlotLevel(FE_PLOT_MINOR_ITRS);
		else if (strcmp(szval, "PLOT_MUST_POINTS") == 0) pstep->SetPlotLevel(FE_PLOT_MUST_POINTS);
		else if (strcmp(szval, "PLOT_FINAL"      ) == 0) pstep->SetPlotLevel(FE_PLOT_FINAL);
		else throw XMLReader::InvalidValue(tag);
	}
	else if (tag == "print_level")
	{
		char szval[256];
		tag.value(szval);
		if      (strcmp(szval, "PRINT_DEFAULT"       ) == 0) {} // don't change the default print level
		else if (strcmp(szval, "PRINT_NEVER"         ) == 0) pstep->SetPrintLevel(FE_PRINT_NEVER);
		else if (strcmp(szval, "PRINT_PROGRESS"      ) == 0) pstep->SetPrintLevel(FE_PRINT_PROGRESS);
		else if (strcmp(szval, "PRINT_MAJOR_ITRS"    ) == 0) pstep->SetPrintLevel(FE_PRINT_MAJOR_ITRS);
		else if (strcmp(szval, "PRINT_MINOR_ITRS"    ) == 0) pstep->SetPrintLevel(FE_PRINT_MINOR_ITRS);
		else if (strcmp(szval, "PRINT_MINOR_ITRS_EXP") == 0) pstep->SetPrintLevel(FE_PRINT_MINOR_ITRS_EXP);
		else throw XMLReader::InvalidTag(tag);
	}
	else if (tag == "use_three_field_hex") tag.value(m_pim->m_b3field);
	else if (tag == "integration")
	{
		++tag;
		do
		{
			if (tag == "rule")
			{
				const char* sze = tag.AttributeValue("elem");
				const char* szv = tag.szvalue();

				if (strcmp(sze, "hex8") == 0)
				{
					if (strcmp(szv, "GAUSS8") == 0) m_pim->m_nhex8 = FE_HEX;
					else if (strcmp(szv, "POINT6") == 0) m_pim->m_nhex8 = FE_RIHEX;
					else if (strcmp(szv, "UDG") == 0) m_pim->m_nhex8 = FE_UDGHEX;
					else throw XMLReader::InvalidValue(tag);
				}
				else if (strcmp(sze, "tet4") == 0)
				{
					if (tag.isleaf())
					{
						if (strcmp(szv, "GAUSS4") == 0) m_pim->m_ntet4 = FEFEBioImport::ET_TET4;
						else if (strcmp(szv, "GAUSS1") == 0) m_pim->m_ntet4 = FEFEBioImport::ET_TETG1;
						else if (strcmp(szv, "UT4") == 0) m_pim->m_ntet4 = FEFEBioImport::ET_UT4;
						else throw XMLReader::InvalidValue(tag);
					}
					else
					{
						const char* szt = tag.AttributeValue("type");
						if (strcmp(szt, "GAUSS4") == 0) m_pim->m_ntet4 = FEFEBioImport::ET_TET4;
						else if (strcmp(szt, "GAUSS1") == 0) m_pim->m_ntet4 = FEFEBioImport::ET_TETG1;
						else if (strcmp(szt, "UT4") == 0) m_pim->m_ntet4 = FEFEBioImport::ET_UT4;
						else throw XMLReader::InvalidAttributeValue(tag, "type", szv);

						++tag;
						do
						{
							if (tag == "alpha") tag.value(FEUT4Domain::m_alpha);
							else if (tag == "iso_stab") tag.value(FEUT4Domain::m_bdev);
							else if (tag == "stab_int")
							{
								const char* sz = tag.szvalue();
								if (strcmp(sz, "GAUSS4") == 0) m_pim->m_nut4 = FE_TET;
								else if (strcmp(sz, "GAUSS1") == 0) m_pim->m_nut4 = FE_TETG1;
							}
							else throw XMLReader::InvalidTag(tag);
							++tag;
						}
						while (!tag.isend());
					}
				}
				else throw XMLReader::InvalidAttributeValue(tag, "elem", sze);
			}
			else throw XMLReader::InvalidValue(tag);
			++tag;
		}
		while (!tag.isend());
	}
	else if (tag == "linear_solver")
	{
		const char* szt = tag.AttributeValue("type");
		if      (strcmp(szt, "skyline"           ) == 0) fem.m_nsolver = SKYLINE_SOLVER;
		else if (strcmp(szt, "psldlt"            ) == 0) fem.m_nsolver = PSLDLT_SOLVER;
		else if (strcmp(szt, "superlu"           ) == 0)
		{
			fem.m_nsolver = SUPERLU_SOLVER;
			if (!tag.isleaf())
			{
				SuperLUSolver* ps = new SuperLUSolver();
				FEAnalysisStep* pstep = GetStep();
				pstep->m_psolver->m_plinsolve = ps;

				++tag;
				do
				{
					if (tag == "print_cnorm") { bool b; tag.value(b); ps->print_cnorm(b); }
					else throw XMLReader::InvalidTag(tag);
					++tag;
				}
				while (!tag.isend());
			}
		}
		else if (strcmp(szt, "superlu_mt"        ) == 0) fem.m_nsolver = SUPERLU_MT_SOLVER;
		else if (strcmp(szt, "pardiso"           ) == 0) fem.m_nsolver = PARDISO_SOLVER;
		else if (strcmp(szt, "wsmp"              ) == 0) fem.m_nsolver = WSMP_SOLVER;
		else if (strcmp(szt, "lusolver"          ) == 0) fem.m_nsolver = LU_SOLVER;
		else if (strcmp(szt, "rcicg"             ) == 0) fem.m_nsolver = RCICG_SOLVER;
		else if (strcmp(szt, "conjugate gradient") == 0)
		{
			fem.m_nsolver = CG_ITERATIVE_SOLVER;
			ConjGradIterSolver* ps;
			FEAnalysisStep* pstep = GetStep();
			pstep->m_psolver->m_plinsolve = ps = new ConjGradIterSolver();
			if (!tag.isleaf())
			{
				++tag;
				do
				{
					if      (tag == "tolerance"     ) tag.value(ps->m_tol);
					else if (tag == "max_iterations") tag.value(ps->m_kmax);
					else if (tag == "print_level"   ) tag.value(ps->m_nprint);
					else throw XMLReader::InvalidTag(tag);
					++tag;
				}
				while (!tag.isend());
			}
		}
		else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
	}
	else return false;

	return true;
}

//=============================================================================
//
//                     M A T E R I A L   S E C T I O N
//
//=============================================================================

void FEBioMaterialSection::Parse(XMLTag& tag)
{
	FEM& fem = *GetFEM();

	// Make sure no materials are defined
	if (fem.Materials() != 0) throw FEFEBioImport::DuplicateMaterialSection();

	const char* sztype = 0;
	const char* szname = 0;

	m_nmat = 0;

	FEBioKernel& febio = FEBioKernel::GetInstance();

	++tag;
	do
	{
		// get the material type
		sztype = tag.AttributeValue("type");

		// get the material name
		szname = tag.AttributeValue("name", true);

		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, &fem);
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);

		// IMPORTANT: depending on the format version number we need to process 
		// rigid bodies differently. For versions <= 0x0100 rigid degrees of 
		// freedom are initially constrained and can be defined in the material
		// section. For versions >= 0x0101 rigid degrees of freedom are free and
		// can be constrained in the Constraints section.
		FERigidMaterial* pm = dynamic_cast<FERigidMaterial*>(pmat);
		if (pm)
		{
			if (m_pim->Version() <= 0x0100)
			{
				// older versions have the rigid degrees of freedom constrained
				for (int i=0; i<6; ++i) pm->m_bc[i] = -1;
			}
			else
			{
				// newer versions have the rigid degrees of freedom unconstrained
				for (int i=0; i<6; ++i) pm->m_bc[i] = 0;
			}
		}

		// add the material
		fem.AddMaterial(pmat);
		++m_nmat;

		// set the material's name
		if (szname) pmat->SetName(szname);

		// set the material's ID
		pmat->SetID(m_nmat);

		ParseMaterial(tag, pmat);

		// read next tag
		++tag;
	}
	while (!tag.isend());

	// assign material pointers for nested materials
	for (int i=0; i<fem.Materials(); ++i)
	{
		FENestedMaterial* pm = dynamic_cast<FENestedMaterial*>(fem.GetMaterial(i));
		// NOTE: The nested version of visco-elasticity is obselete. Until a new
		//		 implementation is available I am using the FENestedMaterial version
		//		 with some tweaks. Only if the m_nBaseMat is not -1 the nested formulation
		//		 is used. If m_nBaseMat == -1 we assume the new formulation and assume that
		//		 the m_pBase is already set.
		if (pm)
		{
			if (pm->m_nBaseMat == -1)
			{
				if (pm->m_pBase == 0) clog.printbox("INPUT ERROR", "base material for material %d is not defined\n", i+1);
			}
			else
			{
				// get the ID of the base material
				// note that m_nBaseMat is a one-based variable!
				int nbase = pm->m_nBaseMat - 1;

				// make sure the base ID is valid
				if ((nbase < 0) || (nbase >= fem.Materials()))
				{
					clog.printbox("INPUT ERROR", "Invalid base material ID for material %d\n", i+1);
					throw XMLReader::Error();
				}

				// make sure the base material is a valid material (i.e. an elastic material)
				FESolidMaterial* pme = dynamic_cast<FESolidMaterial*>(fem.GetMaterial(nbase));

				// don't allow rigid bodies
				if ((pme == 0) || (dynamic_cast<FERigidMaterial*>(pme)))
				{
					clog.printbox("INPUT ERROR", "Invalid base material for material %d\n", i+1);
					throw XMLReader::Error();
				}

				// set the base material pointer
				pm->m_pBase = pme;
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FEBioMaterialSection::ParseMaterial(XMLTag &tag, FEMaterial* pmat)
{
	FEM& fem = *GetFEM();

	// get the material's parameter list
	FEParameterList& pl = pmat->GetParameterList();

	// loop over all parameters
	++tag;
	do
	{
		// see if we can find this parameter
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			// if we get here the parameter was not part of the parameter list
			// however, not all parameters can be read from the parameter lists yet
			// so we have to read in the other parameters the hard way
			// TODO: this option will become obselete
			bool bfound = false;

			// additional elastic material parameters
			if (!bfound && dynamic_cast<FEElasticMaterial*>(pmat)) bfound = ParseElasticMaterial(tag, dynamic_cast<FEElasticMaterial*>(pmat));

			// additional transversely isotropic material parameters
			if (!bfound && dynamic_cast<FETransverselyIsotropic*>(pmat)) bfound = ParseTransIsoMaterial(tag, dynamic_cast<FETransverselyIsotropic*>(pmat));

			// read rigid body data
			if (!bfound && dynamic_cast<FERigidMaterial*>(pmat)) bfound = ParseRigidMaterial(tag, dynamic_cast<FERigidMaterial*>(pmat));

			// elastic mixtures
			if (!bfound && dynamic_cast<FEElasticMixture*>(pmat)) bfound = ParseElasticMixture(tag, dynamic_cast<FEElasticMixture*>(pmat));
			
			// uncoupled elastic mixtures
			if (!bfound && dynamic_cast<FEUncoupledElasticMixture*>(pmat)) bfound = ParseUncoupledElasticMixture(tag, dynamic_cast<FEUncoupledElasticMixture*>(pmat));
			
			// biphasic material parameters
			if (!bfound && dynamic_cast<FEBiphasic*>(pmat)) bfound = ParseBiphasicMaterial(tag, dynamic_cast<FEBiphasic*>(pmat));
			
			// biphasic-solute material parameters
			if (!bfound && dynamic_cast<FEBiphasicSolute*>(pmat)) bfound = ParseBiphasicSoluteMaterial(tag, dynamic_cast<FEBiphasicSolute*>(pmat));

			// solute material parameters
			if (!bfound && dynamic_cast<FESolute*>(pmat)) bfound = ParseSoluteMaterial(tag, dynamic_cast<FESolute*>(pmat));
			
			// triphasic material parameters
			if (!bfound && dynamic_cast<FETriphasic*>(pmat)) bfound = ParseTriphasicMaterial(tag, dynamic_cast<FETriphasic*>(pmat));

			// nested materials
			if (!bfound && dynamic_cast<FENestedMaterial*>(pmat)) bfound = ParseNestedMaterial(tag, dynamic_cast<FENestedMaterial*>(pmat));
			
			// see if we have processed the tag
			if (bfound == false) throw XMLReader::InvalidTag(tag);
		}

		// get the next tag
		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// Parse FEElasticMaterial 
//
bool FEBioMaterialSection::ParseElasticMaterial(XMLTag &tag, FEElasticMaterial *pm)
{
	FEMesh& mesh = m_pim->GetFEM()->GetMesh();
	// read the material axis
	if (tag == "mat_axis")
	{
		const char* szt = tag.AttributeValue("type");
		if (strcmp(szt, "local") == 0)
		{
			FELocalMap* pmap = new FELocalMap(mesh);
			pm->m_pmap = pmap;

			int n[3];
			tag.value(n, 3);
			if ((n[0] == 0) && (n[1] == 0) && (n[2] == 0)) { n[0] = 1; n[1] = 2; n[2] = 4; }

			pmap->SetLocalNodes(n[0]-1, n[1]-1, n[2]-1);
		}
		else if (strcmp(szt, "vector") == 0)
		{
			FEVectorMap* pmap = new FEVectorMap();
			pm->m_pmap = pmap;

			vec3d a(1,0,0), d(0,1,0);
			++tag;
			do
			{
				if (tag == "a") tag.value(a);
				else if (tag == "d") tag.value(d);
				else throw XMLReader::InvalidTag(tag);

				++tag;
			}
			while (!tag.isend());
			pmap->SetVectors(a, d);
		}
		else if (strcmp(szt, "user") == 0)
		{
			// material axis are read in from the ElementData section
		}
		else throw XMLReader::InvalidAttributeValue(tag, "type", szt);

		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Parse FETransverselyIsotropic
//
bool FEBioMaterialSection::ParseTransIsoMaterial(XMLTag &tag, FETransverselyIsotropic *pm)
{
	FEMesh& mesh = m_pim->GetFEM()->GetMesh();

	// read material fibers
	if (tag == "fiber")
	{
		const char* szt = tag.AttributeValue("type");
		if (strcmp(szt, "local") == 0)
		{
			FELocalMap* pmap = new FELocalMap(mesh);
			pm->m_pmap = pmap;

			int n[3] = {0};
			tag.value(n, 2);

			if ((n[0]==0)&&(n[1]==0)&&(n[2]==0)) { n[0] = 1; n[1] = 2; n[2] = 4; }
			if (n[2] == 0) n[2] = n[1];

			pmap->SetLocalNodes(n[0]-1, n[1]-1, n[2]-1);
		}
		else if (strcmp(szt, "spherical") == 0)
		{
			FESphericalMap* pmap = new FESphericalMap(mesh);
			pm->m_pmap = pmap;

			vec3d c;
			tag.value(c);

			pmap->SetSphereCenter(c);
		}
		else if (strcmp(szt, "vector") == 0)
		{
			FEVectorMap* pmap = new FEVectorMap;
			pm->m_pmap = pmap;

			vec3d a, d;
			tag.value(a);
			a.unit();

			d = vec3d(1,0,0);
			if (a*d > .999) d = vec3d(0,1,0);

			pmap->SetVectors(a, d);
		}
		else if (strcmp(szt, "user") == 0)
		{
			// fibers are read in in the ElementData section
		}
		else throw XMLReader::InvalidAttributeValue(tag, "type", szt);

		// mark the tag as read
		return true;
	}
	else if (tag == "active_contraction")
	{
		const char* szlc = tag.AttributeValue("lc");
		assert(szlc);
		FEParameterList& pl = pm->m_fib.GetParameterList();
		FEParam& p = *pl.Find("ascl");
		p.m_nlc = atoi(szlc);
		p.value<double>() = 1.0;

		++tag;
		do
		{
			if (tag == "ca0") tag.value(pm->m_fib.m_ca0);
			else if (tag == "beta") tag.value(pm->m_fib.m_beta);
			else if (tag == "l0") tag.value(pm->m_fib.m_l0);
			else if (tag == "refl") tag.value(pm->m_fib.m_refl);
			else throw XMLReader::InvalidTag(tag);
			++tag;
		}
		while (!tag.isend());

		// mark tag as read
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
// Read rigid materials
//
bool FEBioMaterialSection::ParseRigidMaterial(XMLTag &tag, FERigidMaterial *pm)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pStep = GetStep();

	if (tag == "center_of_mass") { tag.value(pm->m_rc); pm->m_com = 1; return true; }
	else if (tag == "parent_id") { tag.value(pm->m_pmid); return true; }
	else if (m_pim->Version() <= 0x0100)
	{
		// The following tags are only allowed in older version of FEBio
		// Newer versions defined the rigid body constraints in the Constraints section
		if (strncmp(tag.Name(), "trans_", 6) == 0)
		{
			const char* szt = tag.AttributeValue("type");
			const char* szlc = tag.AttributeValue("lc", true);
			int lc = 0;
			if (szlc) lc = atoi(szlc)+1;

			int bc = -1;
			if      (tag.Name()[6] == 'x') bc = 0;
			else if (tag.Name()[6] == 'y') bc = 1;
			else if (tag.Name()[6] == 'z') bc = 2;
			assert(bc >= 0);

			if      (strcmp(szt, "free"      ) == 0) pm->m_bc[bc] =  0;
			else if (strcmp(szt, "fixed"     ) == 0) pm->m_bc[bc] = -1;
			else if (strcmp(szt, "prescribed") == 0)
			{
				pm->m_bc[bc] = lc;
				FERigidBodyDisplacement* pDC = new FERigidBodyDisplacement;
				pDC->id = m_nmat;
				pDC->bc = bc;
				pDC->lc = lc;
				tag.value(pDC->sf);
				fem.m_RDC.push_back(pDC);

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					int n = fem.m_RDC.size()-1;
					FERigidBodyDisplacement* pDC = fem.m_RDC[n];
					pStep->AddBoundaryCondition(pDC);
					pDC->Deactivate();
				}
			}
			else if (strcmp(szt, "force") == 0)
			{
				pm->m_bc[bc] = 0;
				FERigidBodyForce* pFC = new FERigidBodyForce;
				pFC->id = m_nmat;
				pFC->bc = bc;
				pFC->lc = lc-1;
				tag.value(pFC->sf);
				fem.m_RFC.push_back(pFC);

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					int n = fem.m_RFC.size()-1;
					FERigidBodyForce* pFC = fem.m_RFC[n];
					pStep->AddBoundaryCondition(pFC);
					pFC->Deactivate();
				}
			}
			else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
			return true;
		}
		else if (strncmp(tag.Name(), "rot_", 4) == 0)
		{
			const char* szt = tag.AttributeValue("type");
			const char* szlc = tag.AttributeValue("lc", true);
			int lc = 0;
			if (szlc) lc = atoi(szlc)+1;

			int bc = -1;
			if      (tag.Name()[4] == 'x') bc = 3;
			else if (tag.Name()[4] == 'y') bc = 4;
			else if (tag.Name()[4] == 'z') bc = 5;
			assert(bc >= 0);

			if      (strcmp(szt, "free"      ) == 0) pm->m_bc[bc] =  0;
			else if (strcmp(szt, "fixed"     ) == 0) pm->m_bc[bc] = -1;
			else if (strcmp(szt, "prescribed") == 0)
			{
				pm->m_bc[bc] = lc;
				FERigidBodyDisplacement* pDC = new FERigidBodyDisplacement;
				pDC->id = m_nmat;
				pDC->bc = bc;
				pDC->lc = lc;
				tag.value(pDC->sf);
				fem.m_RDC.push_back(pDC);

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					int n = fem.m_RDC.size()-1;
					FERigidBodyDisplacement* pDC = fem.m_RDC[n];
					pStep->AddBoundaryCondition(pDC);
					pDC->Deactivate();
				}
			}
			else if (strcmp(szt, "force") == 0)
			{
				pm->m_bc[bc] = 0;
				FERigidBodyForce* pFC = new FERigidBodyForce;
				pFC->id = m_nmat;
				pFC->bc = bc;
				pFC->lc = lc-1;
				tag.value(pFC->sf);
				fem.m_RFC.push_back(pFC);

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					int n = fem.m_RFC.size()-1;
					FERigidBodyForce* pFC = fem.m_RFC[n];
					pStep->AddBoundaryCondition(pFC);
					pFC->Deactivate();
				}
			}
			else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Parse FEElasticMixture material 
//
bool FEBioMaterialSection::ParseElasticMixture(XMLTag &tag, FEElasticMixture *pm)
{
	const char* sztype = 0;
	const char* szname = 0;

	FEBioKernel& febio = FEBioKernel::GetInstance();
	
	// read the solid material
	if (tag == "solid")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. an elastic material)
		FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmat);
		
		// don't allow rigid bodies
		if ((pme == 0) || (dynamic_cast<FERigidMaterial*>(pme)))
		{
			clog.printbox("INPUT ERROR", "Invalid elastic solid %s in solid mixture material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solid material pointer
		pm->m_pMat.push_back(pme);
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// TODO: assume that the material becomes stable since it is combined with others
		// in a solid mixture.  (This may not necessarily be true.)
		pme->m_unstable = false;

		// parse the solid
		ParseMaterial(tag, pme);
		
		return true;
	}
	else throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
	
	return false;
}

//-----------------------------------------------------------------------------
// Parse FEUncoupledElasticMixture material 
//
bool FEBioMaterialSection::ParseUncoupledElasticMixture(XMLTag &tag, FEUncoupledElasticMixture *pm)
{
	const char* sztype = 0;
	const char* szname = 0;

	FEBioKernel& febio = FEBioKernel::GetInstance();
	
	// read the solid material
	if (tag == "solid")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. an uncoupled elastic material)
		FEUncoupledMaterial* pme = dynamic_cast<FEUncoupledMaterial*>(pmat);
		
		// don't allow rigid bodies
		if ((pme == 0) || (dynamic_cast<FERigidMaterial*>(pme)))
		{
			clog.printbox("INPUT ERROR", "Invalid uncoupled elastic solid %s in uncoupled solid mixture material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solid material pointer
		pm->m_pMat.push_back(pme);
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// TODO: assume that the material becomes stable since it is combined with others
		// in a solid mixture.  (This may not necessarily be true.)
		pme->m_unstable = false;
		
		// parse the solid
		ParseMaterial(tag, pme);
		
		return true;
	}
	else throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
	
	return false;
}

//-----------------------------------------------------------------------------
// Parse FEBiphasic material 
//
bool FEBioMaterialSection::ParseBiphasicMaterial(XMLTag &tag, FEBiphasic *pm)
{
	const char* sztype = 0;
	const char* szname = 0;

	FEBioKernel& febio = FEBioKernel::GetInstance();
	
	// read the solid material
	if (tag == "solid")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. an elastic material)
		FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmat);
		
		// don't allow rigid bodies
		if ((pme == 0) || (dynamic_cast<FERigidMaterial*>(pme)))
		{
			clog.printbox("INPUT ERROR", "Invalid elastic solid %s in biphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solid material pointer
		pm->m_pSolid = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the solid
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "permeability")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a permeability material)
		FEHydraulicPermeability* pme = dynamic_cast<FEHydraulicPermeability*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid permeability %s in biphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the permeability pointer
		pm->m_pPerm = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the solid
		ParseMaterial(tag, pme);
		
		return true;
	}
	else throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
	
	return false;
}

//-----------------------------------------------------------------------------
// Parse FEBiphasicSolute material 
//
bool FEBioMaterialSection::ParseBiphasicSoluteMaterial(XMLTag &tag, FEBiphasicSolute *pm)
{
	const char* sztype = 0;
	const char* szname = 0;

	FEBioKernel& febio = FEBioKernel::GetInstance();
	
	// read the solid material
	if (tag == "solid")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. an elastic material)
		FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmat);
		
		// don't allow rigid bodies
		if ((pme == 0) || (dynamic_cast<FERigidMaterial*>(pme)))
		{
			clog.printbox("INPUT ERROR", "Invalid elastic solid %s in biphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solid material pointer
		pm->m_pSolid = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the solid
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "permeability")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a permeability material)
		FEHydraulicPermeability* pme = dynamic_cast<FEHydraulicPermeability*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid permeability %s in biphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the permeability pointer
		pm->m_pPerm = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "diffusivity")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a diffusivity material)
		FESoluteDiffusivity* pme = dynamic_cast<FESoluteDiffusivity*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid diffusivity %s in biphasic-solute material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the diffusivity pointer
		pm->m_pDiff = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// set solute ID
		pme->SetSoluteID(0);

		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "solubility")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a solubility material)
		FESoluteSolubility* pme = dynamic_cast<FESoluteSolubility*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid solubility %s in biphasic-solute material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solubility pointer
		pm->m_pSolub = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);

		// set solute ID
		pme->SetSoluteID(0);
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "osmotic_coefficient")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a osmotic coefficient material)
		FEOsmoticCoefficient* pme = dynamic_cast<FEOsmoticCoefficient*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid osmotic coefficient %s in biphasic-solute material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the osmotic coefficient pointer
		pm->m_pOsmC = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "supply")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a osmotic coefficient material)
		FESoluteSupply* pme = dynamic_cast<FESoluteSupply*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid supply %s in biphasic-solute material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the osmotic coefficient pointer
		pm->m_pSupp = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
	
	return false;
}

//-----------------------------------------------------------------------------
// Parse FESolute material 
//
bool FEBioMaterialSection::ParseSoluteMaterial(XMLTag &tag, FESolute *pm)
{
	const char* sztype = 0;
	const char* szname = 0;
	
	FEBioKernel& febio = FEBioKernel::GetInstance();
	
	// read the diffusivity material
	if (tag == "diffusivity")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a diffusivity material)
		FESoluteDiffusivity* pme = dynamic_cast<FESoluteDiffusivity*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid diffusivity %s in solute material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the diffusivity pointer
		pm->m_pDiff = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// set solute ID in diffusivity
		pme->SetSoluteID(pm->GetSoluteID());
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "solubility")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a solubility material)
		FESoluteSolubility* pme = dynamic_cast<FESoluteSolubility*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid solubility %s in solute material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solubility pointer
		pm->m_pSolub = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// set solute ID in solubility
		pme->SetSoluteID(pm->GetSoluteID());
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
	
	return false;
}

//-----------------------------------------------------------------------------
// Parse FETriphasic material 
//
bool FEBioMaterialSection::ParseTriphasicMaterial(XMLTag &tag, FETriphasic *pm)
{
	const char* sztype = 0;
	const char* szname = 0;
	const char* szid = 0;
	
	FEBioKernel& febio = FEBioKernel::GetInstance();
	
	// read the solid material
	if (tag == "solid")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. an elastic material)
		FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmat);
		
		// don't allow rigid bodies
		if ((pme == 0) || (dynamic_cast<FERigidMaterial*>(pme)))
		{
			clog.printbox("INPUT ERROR", "Invalid elastic solid %s in triphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solid material pointer
		pm->m_pSolid = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the solid
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "permeability")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a permeability material)
		FEHydraulicPermeability* pme = dynamic_cast<FEHydraulicPermeability*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid permeability %s in triphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the permeability pointer
		pm->m_pPerm = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "solute")
	{
		// get the material type
//		sztype = tag.AttributeValue("type");
		sztype = "solute";
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// get the solute id
		int id;
		szid = tag.AttributeValue("id");
		if (szid) {
			id = atoi(szid) - 1;
			if ((id < 0) || (id > 1))
				throw XMLReader::InvalidAttributeValue(tag, "id", szid);
		} else {
			throw XMLReader::MissingAttribute(tag, "id");
		}

		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a solute material)
		FESolute* pme = dynamic_cast<FESolute*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid solute %s in triphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solute pointer
		pm->m_pSolute[id] = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// set the solute ID
		pme->SetID(id);
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else if (tag == "osmotic_coefficient")
	{
		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. a osmotic coefficient material)
		FEOsmoticCoefficient* pme = dynamic_cast<FEOsmoticCoefficient*>(pmat);
		
		if (pme == 0)
		{
			clog.printbox("INPUT ERROR", "Invalid osmotic coefficient %s in triphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the osmotic coefficient pointer
		pm->m_pOsmC = pme;
		
		// set the material's name
		if (szname) pme->SetName(szname);
		
		// parse the material
		ParseMaterial(tag, pme);
		
		return true;
	}
	else throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
	
	return false;
}

//-----------------------------------------------------------------------------
// Parse a nested material
// NOTE: nested materials are obselete, but we are still using them in the
// interest of backward compatibility. At this point, they are somewhat of a
// hybrid between the old and new format.
bool FEBioMaterialSection::ParseNestedMaterial(XMLTag &tag, FENestedMaterial *pm)
{
	FEBioKernel& febio = FEBioKernel::GetInstance();

	// Make sure the m_nBaseMat is -1
	if (pm->m_nBaseMat != -1) return false;
	
	// read the solid material
	if (tag == "elastic")
	{
		const char* sztype = 0;
		const char* szname = 0;

		// get the material type
		sztype = tag.AttributeValue("type");
		
		// get the material name
		szname = tag.AttributeValue("name", true);
		
		// create a new material of this type
		FEMaterial* pmat = febio.Create<FEMaterial>(sztype, GetFEM());
		if (pmat == 0) throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		
		// make sure the base material is a valid material (i.e. an elastic material)
		FEElasticMaterial* pme = dynamic_cast<FEElasticMaterial*>(pmat);
		
		// don't allow rigid bodies
		if ((pme == 0) || (dynamic_cast<FERigidMaterial*>(pme)))
		{
			clog.printbox("INPUT ERROR", "Invalid elastic solid %s in biphasic material %s\n", szname, pm->GetName());
			throw XMLReader::Error();
		}
		
		// set the solid material pointer
		pm->m_pBase = pme;
		
		// parse the solid
		ParseMaterial(tag, pme);
		
		return true;
	}

	return false;
}

//=============================================================================
//
//                       G E O M E T R Y   S E C T I O N
//
//=============================================================================
//!  Parses the geometry section from the xml file
//!
void FEBioGeometrySection::Parse(XMLTag& tag)
{
	if (m_pim->Version() < 0x0200)
	{
		++tag;
		do
		{
			if      (tag == "Nodes"      ) ParseNodeSection       (tag);
			else if (tag == "Elements"   ) ParseElementSection    (tag);
			else if (tag == "ElementData") ParseElementDataSection(tag);
			else throw XMLReader::InvalidTag(tag);
			++tag;
		}
		while (!tag.isend());
	}
	else
	{
		++tag;
		do
		{
			if      (tag == "Nodes"      ) ParseNodeSection       (tag);
			else if (tag == "Elements"   ) ParseElementSection    (tag);
			else if (tag == "ElementData") ParseElementDataSection(tag);
			else if (tag == "NodeSet"    ) ParseNodeSetSection    (tag);
			else if (tag == "Part"       ) ParsePartSection       (tag);
			else throw XMLReader::InvalidTag(tag);
			++tag;
		}
		while (!tag.isend());
	}
}

//-----------------------------------------------------------------------------
//! Reads the Nodes section of the FEBio input file
void FEBioGeometrySection::ParseNodeSection(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;
	int N0 = mesh.Nodes();

	// first we need to figure out how many nodes there are
	XMLTag t(tag);
	int nodes = 0;
	++t;
	while (!t.isend()) { nodes++; ++t; }

	// see if this list defines a set
	const char* szl = tag.AttributeValue("set", true);
	FENodeSet* ps = 0;
	if (szl)
	{
		ps = new FENodeSet(&mesh);
		ps->SetName(szl);
		ps->create(nodes);
		mesh.AddNodeSet(ps);
	}

	// resize node's array
	mesh.AddNodes(nodes);

	// read nodal coordinates
	++tag;
	for (int i=0; i<nodes; ++i)
	{
		FENode& node = fem.m_mesh.Node(N0 + i);
		tag.value(node.m_r0);
		node.m_rt = node.m_r0;

		// set rigid body id
		node.m_rid = -1;

		// open displacement dofs
		node.m_ID[DOF_X] = 0;
		node.m_ID[DOF_Y] = 0;
		node.m_ID[DOF_Z] = 0;

		// open rotational dofs
		node.m_ID[DOF_U] = 0;
		node.m_ID[DOF_V] = 0;
		node.m_ID[DOF_W] = 0;

		// open pressure dof
		node.m_ID[DOF_P] = 0;

		// close the rigid rotational dofs
		node.m_ID[DOF_RU] = -1;
		node.m_ID[DOF_RV] = -1;
		node.m_ID[DOF_RW] = -1;

		// fix temperature dof
		node.m_ID[DOF_T] = -1;

		// open concentration dof
		node.m_ID[DOF_C] = 0;
		
		++tag;
	}

	// If a node-set is defined add these nodes to the node-set
	if (ps)
	{
		for (int i=0; i<nodes; ++i) (*ps)[i] = N0+i;
	}

	// open temperature dofs for heat-transfer problems
	if (fem.m_pStep->m_nModule == FE_HEAT)
	{
		for (int i=0; i<nodes; ++i) 
		{
			FENode& n = fem.m_mesh.Node(i);
			for (int j=0; j<MAX_NDOFS; ++j) n.m_ID[j] = -1;
			n.m_ID[DOF_T] = 0;
		}
	}

	// open temperature and displacement dofs 
	// for coupled heat-solid problems
	if (fem.m_pStep->m_nModule == FE_HEAT_SOLID)
	{
		for (int i=0; i<nodes; ++i) 
		{
			FENode& n = fem.m_mesh.Node(i);
			for (int j=0; j<MAX_NDOFS; ++j) n.m_ID[j] = -1;
			n.m_ID[DOF_X] = 0;
			n.m_ID[DOF_Y] = 0;
			n.m_ID[DOF_Z] = 0;
			n.m_ID[DOF_T] = 0;
		}
	}
}

//-----------------------------------------------------------------------------
//! Get the element type from a XML tag
int FEBioGeometrySection::ElementType(XMLTag& t)
{
	if (t=="hex8"  ) return ET_HEX;
	if (t=="hex20" ) return ET_HEX20;
	if (t=="penta6") return ET_PENTA;
	if (t=="tet4"  ) return ET_TET;
	if (t=="quad4" ) return ET_QUAD;
	if (t=="tri3"  ) return ET_TRI;
	if (t=="truss2") return ET_TRUSS;
	return -1;
}

//-----------------------------------------------------------------------------
//! find the domain type for the element and material type
int FEBioGeometrySection::DomainType(int etype, FEMaterial* pmat)
{
	FEM& fem = *GetFEM();
	FEMesh* pm = &fem.m_mesh;

	// get the module
	if (fem.m_pStep->m_nModule == FE_HEAT)
	{
		if ((etype == ET_HEX) || (etype == ET_HEX20) || (etype == ET_PENTA) || (etype == ET_TET)) return FE_HEAT_SOLID_DOMAIN;
		else return 0;
	}
	else if (fem.m_pStep->m_nModule == FE_LINEAR_SOLID)
	{
		if ((etype == ET_HEX) || (etype == ET_PENTA) || (etype == ET_TET)) return FE_LINEAR_SOLID_DOMAIN;
		else return 0;
	}
	else if (fem.m_pStep->m_nModule == FE_HEAT_SOLID)
	{
		if ((etype == ET_HEX) || (etype == ET_PENTA) || (etype == ET_TET))
		{
			if (dynamic_cast<FEHeatTransferMaterial*>(pmat)) return FE_HEAT_SOLID_DOMAIN;
			else return FE_LINEAR_SOLID_DOMAIN;
		}
		else return 0;
	}
	else
	{
		if (dynamic_cast<FERigidMaterial*>(pmat))
		{
			// rigid elements
			if ((etype == ET_HEX) || (etype == ET_PENTA) || (etype == ET_TET)) return FE_RIGID_SOLID_DOMAIN;
			else if ((etype == ET_QUAD) || (etype == ET_TRI)) return FE_RIGID_SHELL_DOMAIN;
			else return 0;
		}
		else if (dynamic_cast<FEBiphasic*>(pmat))
		{
			// biphasic elements
			if ((etype == ET_HEX) || (etype == ET_PENTA) || (etype == ET_TET)) return FE_BIPHASIC_DOMAIN;
			else return 0;
		}
		else if (dynamic_cast<FEBiphasicSolute*>(pmat))
		{
			// biphasic elements
			if ((etype == ET_HEX) || (etype == ET_PENTA) || (etype == ET_TET)) return FE_BIPHASIC_SOLUTE_DOMAIN;
			else return 0;
		}
		else
		{
			// structural elements
			if (etype == ET_HEX)
			{
				// three-field implementation for uncoupled materials
				if (dynamic_cast<FEUncoupledMaterial*>(pmat) && m_pim->m_b3field) return FE_3F_SOLID_DOMAIN;
				else
				{
					if (m_pim->m_nhex8 == FE_UDGHEX) return FE_UDGHEX_DOMAIN;
					else return FE_SOLID_DOMAIN;
				}
			}
			else if (etype == ET_TET)
			{
				if (m_pim->m_ntet4 == FEFEBioImport::ET_UT4) return FE_UT4_DOMAIN;
				else return FE_SOLID_DOMAIN;
			}
			else if (etype == ET_PENTA) 
			{
				// three-field implementation for uncoupled materials
				if (dynamic_cast<FEUncoupledMaterial*>(pmat)) return FE_3F_SOLID_DOMAIN;
				else return FE_SOLID_DOMAIN;
			}
			else if ((etype == ET_QUAD) || (etype == ET_TRI)) return FE_SHELL_DOMAIN;
			else if ((etype == ET_TRUSS)) return FE_TRUSS_DOMAIN;
			else return 0;
		}
	}

	return 0;
}

//-----------------------------------------------------------------------------
//! Create a particular type of domain
FEDomain* FEBioGeometrySection::CreateDomain(int ntype, FEMesh* pm, FEMaterial* pmat)
{
	// create a new domain based on the type
	FEDomain* pd = 0;
	switch (ntype)
	{
	case FE_SOLID_DOMAIN          : pd = new FEElasticSolidDomain      (pm, pmat); break;
	case FE_SHELL_DOMAIN          : pd = new FEElasticShellDomain      (pm, pmat); break;
	case FE_TRUSS_DOMAIN          : pd = new FEElasticTrussDomain      (pm, pmat); break;
	case FE_RIGID_SOLID_DOMAIN    : pd = new FERigidSolidDomain        (pm, pmat); break;
	case FE_RIGID_SHELL_DOMAIN    : pd = new FERigidShellDomain        (pm, pmat); break;
	case FE_UDGHEX_DOMAIN         : pd = new FEUDGHexDomain            (pm, pmat); break;
	case FE_UT4_DOMAIN            : pd = new FEUT4Domain               (pm, pmat); break;
	case FE_HEAT_SOLID_DOMAIN     : pd = new FEHeatSolidDomain         (pm, pmat); break;
	case FE_3F_SOLID_DOMAIN       : pd = new FE3FieldElasticSolidDomain(pm, pmat); break;
	case FE_BIPHASIC_DOMAIN       : pd = new FEBiphasicDomain          (pm, pmat); break;
	case FE_BIPHASIC_SOLUTE_DOMAIN: pd = new FEBiphasicSoluteDomain    (pm, pmat); break;
	case FE_LINEAR_SOLID_DOMAIN   : pd = new FELinearSolidDomain       (pm, pmat); break;
	}

	// return the domain
	return pd;
}

//-----------------------------------------------------------------------------
//! This function reads the Element section from the FEBio input file. It also
//! creates the domain classes which store the element data. A domain is defined
//! by the module (structural, poro, heat, etc), the element type (solid, shell,
//! etc.) and the material. 
//!
void FEBioGeometrySection::ParseElementSection(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;

	// first we need to figure out how many elements 
	// and how many domains there are
	vector<FEDOMAIN> dom;
	vector<int>	ED; ED.reserve(1000);
	XMLTag t(tag); ++t;
	int elems = 0, i;
	while (!t.isend())
	{
		// get the material ID
		const char* szmat = t.AttributeValue("mat");
		int nmat = atoi(szmat)-1;
		if ((nmat < 0) || (nmat >= fem.Materials())) throw FEFEBioImport::InvalidMaterial(elems+1);

		// get the element type
		int etype = ElementType(t);
		if (etype < 0) throw XMLReader::InvalidTag(t);

		// find a domain for this element
		int ndom = -1;
		for (i=0; i<(int) dom.size(); ++i)
		{
			FEDOMAIN& d = dom[i];
			if ((d.mat == nmat) && (d.elem == etype))
			{
				ndom = i;
				d.nel++;
				break;
			}
		}
		if (ndom == -1)
		{
			FEDOMAIN d;
			d.mat = nmat;
			d.elem = etype;
			d.nel = 1;
			ndom = (int) dom.size();
			dom.push_back(d);
		}

		ED.push_back(ndom);
		elems++;
		++t;
	}

	// create the domains
	for (i=0; i<(int) dom.size(); ++i)
	{
		FEDOMAIN& d = dom[i];

		// get material class
		FEMaterial* pmat = fem.GetMaterial(d.mat);

		// then, find the domain type depending on the 
		// element and material types
		int ntype = DomainType(d.elem, pmat);
		if (ntype == 0) throw FEFEBioImport::InvalidDomainType();

		// create the new domain
		FEDomain* pdom = CreateDomain(ntype, &mesh, pmat);
		if (pdom == 0) throw FEFEBioImport::FailedCreatingDomain();


		// add it to the mesh
		assert(d.nel);
		pdom->create(d.nel);
		mesh.AddDomain(pdom);

		// we reset the nr of elements since we'll be using 
		// that variable is a counter in the next loop
		d.nel = 0;
	}

	// read element data
	++tag;
	int nid = 1;
	for (i=0; i<elems; ++i, ++nid)
	{
		int nd = ED[i];
		int ne = dom[nd].nel++;

		// get the domain to which this element belongs
		FEDomain& dom = mesh.Domain(nd);

		// get the material ID
		int nmat = atoi(tag.AttributeValue("mat"))-1;

		// get the material class
		FEMaterial* pmat = fem.GetMaterial(nmat);
		assert(pmat == dom.GetMaterial());

		// determine element type
		int etype = -1;
		if      (tag == "hex8"  ) etype = FEFEBioImport::ET_HEX8;
		else if (tag == "hex20" ) etype = FEFEBioImport::ET_HEX20;
		else if (tag == "penta6") etype = FEFEBioImport::ET_PENTA6;
		else if (tag == "tet4"  ) etype = m_pim->m_ntet4;
		else if (tag == "quad4" ) etype = FEFEBioImport::ET_QUAD4;
		else if (tag == "tri3"  ) etype = FEFEBioImport::ET_TRI3;
		else if (tag == "truss2") etype = FEFEBioImport::ET_TRUSS2;
		else throw XMLReader::InvalidTag(tag);

		switch (etype)
		{
		case FEFEBioImport::ET_HEX8:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), m_pim->m_nhex8, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_HEX20:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), FE_HEX20, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_PENTA6:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), FE_PENTA, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_TET4:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), FE_TET, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_UT4:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), m_pim->m_nut4, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_TETG1:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), FE_TETG1, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_QUAD4:
			{
				FEShellDomain& sd = dynamic_cast<FEShellDomain&>(dom);
				ReadShellElement(tag, sd.Element(ne), FE_SHELL_QUAD, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_TRI3:
			{
				FEShellDomain& sd = dynamic_cast<FEShellDomain&>(dom);
				ReadShellElement(tag, sd.Element(ne), FE_SHELL_TRI, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_TRUSS2:
			{
				FETrussDomain& td = dynamic_cast<FETrussDomain&>(dom);
				ReadTrussElement(tag, td.Element(ne), FE_TRUSS, nid, nmat);
			}
			break;
		default:
			throw FEFEBioImport::InvalidElementType();
		}

		// go to next tag
		++tag;
	}

	// assign material point data
	for (i=0; i<mesh.Domains(); ++i)
	{
		FEDomain& d = mesh.Domain(i);
		d.InitMaterialPointData();
	}
}

//-----------------------------------------------------------------------------
void FEBioGeometrySection::ParsePartSection(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;

	// get the element type
	const char* szel = tag.AttributeValue("elem");

	// determine element type
	int etype = -1;
	if      (strcmp(szel, "hex8"  ) == 0) etype = ET_HEX;
	else if (strcmp(szel, "hex20" ) == 0) etype = ET_HEX20;
	else if (strcmp(szel, "penta6") == 0) etype = ET_PENTA;
	else if (strcmp(szel, "tet4"  ) == 0) etype = ET_TET;
	else if (strcmp(szel, "quad4" ) == 0) etype = ET_QUAD;
	else if (strcmp(szel, "tri3"  ) == 0) etype = ET_TRI;
	else if (strcmp(szel, "truss2") == 0) etype = ET_TRUSS;
	else throw XMLReader::InvalidAttributeValue(tag, "elem", szel);

	// get the material ID
	const char* szmat = tag.AttributeValue("mat");
	int nmat = atoi(szmat)-1;
	if ((nmat < 0) || (nmat >= fem.Materials())) throw XMLReader::InvalidAttributeValue(tag, "mat", szmat);

	// Count the elements
	XMLTag t(tag); ++t;
	int nelems = 0;
	while (!t.isend())
	{
		nelems++;
		++t;
	}

	// get the material class
	FEMaterial* pmat = fem.GetMaterial(nmat);
	assert(pmat);

	// then, find the domain type depending on the 
	// element and material types
	int ndom = DomainType(etype, pmat);
	if (ndom == 0) throw FEFEBioImport::InvalidDomainType();

	// create the new domain
	FEDomain* pdom = CreateDomain(ndom, &mesh, pmat);
	if (pdom == 0) throw FEFEBioImport::FailedCreatingDomain();
	mesh.AddDomain(pdom);
	FEDomain& dom = *pdom;
	dom.create(nelems);

	// determine element type
	if      (etype == ET_HEX  ) etype = FEFEBioImport::ET_HEX8;
	else if (etype == ET_PENTA) etype = FEFEBioImport::ET_PENTA6;
	else if (etype == ET_TET  ) etype = m_pim->m_ntet4;
	else if (etype == ET_QUAD ) etype = FEFEBioImport::ET_QUAD4;
	else if (etype == ET_TRI  ) etype = FEFEBioImport::ET_TRI3;
	else if (etype == ET_TRUSS) etype = FEFEBioImport::ET_TRUSS2;
	else throw XMLReader::InvalidTag(tag);

	// read all elements 
	++tag;
	for (int ne=0; ne<nelems; ++ne)
	{
		int nid = atoi(tag.AttributeValue("id"));

		switch (etype)
		{
		case FEFEBioImport::ET_HEX8:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), m_pim->m_nhex8, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_PENTA6:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), FE_PENTA, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_TET4:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), FE_TET, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_UT4:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), m_pim->m_nut4, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_TETG1:
			{
				FESolidDomain& bd = dynamic_cast<FESolidDomain&>(dom);
				ReadSolidElement(tag, bd.Element(ne), FE_TETG1, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_QUAD4:
			{
				FEShellDomain& sd = dynamic_cast<FEShellDomain&>(dom);
				ReadShellElement(tag, sd.Element(ne), FE_SHELL_QUAD, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_TRI3:
			{
				FEShellDomain& sd = dynamic_cast<FEShellDomain&>(dom);
				ReadShellElement(tag, sd.Element(ne), FE_SHELL_TRI, nid, nmat);
			}
			break;
		case FEFEBioImport::ET_TRUSS2:
			{
				FETrussDomain& td = dynamic_cast<FETrussDomain&>(dom);
				ReadTrussElement(tag, td.Element(ne), FE_TRUSS, nid, nmat);
			}
			break;
		default:
			throw FEFEBioImport::InvalidElementType();
		}

		// go to the next tag
		++tag;
	}

	// assign material point data
	dom.InitMaterialPointData();
}

//-----------------------------------------------------------------------------
void FEBioGeometrySection::ReadSolidElement(XMLTag &tag, FESolidElement& el, int ntype, int nid, int nmat)
{
	el.SetType(ntype);
	el.m_nID = nid;
	int n[FEElement::MAX_NODES];
	tag.value(n,el.Nodes());
	for (int j=0; j<el.Nodes(); ++j) el.m_node[j] = n[j]-1;
	el.SetMatID(nmat);
}

//-----------------------------------------------------------------------------
void FEBioGeometrySection::ReadShellElement(XMLTag &tag, FEShellElement& el, int ntype, int nid, int nmat)
{
	el.SetType(ntype);
	el.m_nID = nid;
	int n[8];
	tag.value(n,el.Nodes());
	for (int j=0; j<el.Nodes(); ++j) { el.m_node[j] = n[j]-1; el.m_h0[j] = 0.0; }
	el.SetMatID(nmat);
}

//-----------------------------------------------------------------------------
void FEBioGeometrySection::ReadTrussElement(XMLTag &tag, FETrussElement& el, int ntype, int nid, int nmat)
{
	el.SetType(ntype);
	el.m_nID = nid;
	int n[8];
	tag.value(n, el.Nodes());
	for (int j=0; j<el.Nodes(); ++j) el.m_node[j] = n[j]-1;
	el.SetMatID(nmat);

	// area is read in the ElementData section
	el.m_a0 = 0;
}


//-----------------------------------------------------------------------------
//! Reads the ElementData section from the FEBio input file

void FEBioGeometrySection::ParseElementDataSection(XMLTag& tag)
{
	int i;

	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;

	// get the total nr of elements
	int nelems = mesh.Elements();

	//make sure we've read the element section
	if (nelems == 0) throw XMLReader::InvalidTag(tag);

	// create the pelem array
	vector<FEElement*> pelem;
	pelem.assign(nelems, static_cast<FEElement*>(0));

	for (int nd=0; nd<mesh.Domains(); ++nd)
	{
		FEDomain& d = mesh.Domain(nd);
		for (i=0; i<d.Elements(); ++i)
		{
			FEElement& el = d.ElementRef(i);
			assert(pelem[el.m_nID-1] == 0);
			pelem[el.m_nID-1] = &el;
		}
	}

	// read additional element data
	++tag;
	do
	{
		// make sure this is an "element" tag
		if (tag != "element") throw XMLReader::InvalidTag(tag);

		// get the element number
		const char* szid = tag.AttributeValue("id");
		int n = atoi(szid)-1;

		// make sure the number is valid
		if ((n<0) || (n>=nelems)) throw XMLReader::InvalidAttributeValue(tag, "id", szid);

		// get a pointer to the element
		FEElement* pe = pelem[n];

		vec3d a;
		++tag;
		do
		{
			if (tag == "fiber")
			{
				// read the fiber direction
				tag.value(a);

				// normalize fiber
				a.unit();

				// set up a orthonormal coordinate system
				vec3d b(0,1,0);
				if (fabs(fabs(a*b) - 1) < 1e-7) b = vec3d(0,0,1);
				vec3d c = a^b;
				b = c^a;

				// make sure they are unit vectors
				b.unit();
				c.unit();

				FESolidElement* pbe = dynamic_cast<FESolidElement*> (pe);
				FEShellElement* pse = dynamic_cast<FEShellElement*> (pe);
				if (pbe)
				{
					for (int i=0; i<pbe->GaussPoints(); ++i)
					{
						FEElasticMaterialPoint& pt = *pbe->m_State[i]->ExtractData<FEElasticMaterialPoint>();
						mat3d& m = pt.Q;
						m.zero();
						m[0][0] = a.x; m[0][1] = b.x; m[0][2] = c.x;
						m[1][0] = a.y; m[1][1] = b.y; m[1][2] = c.y;
						m[2][0] = a.z; m[2][1] = b.z; m[2][2] = c.z;
					}
				}
				if (pse)
				{
					for (int i=0; i<pse->GaussPoints(); ++i)
					{
						FEElasticMaterialPoint& pt = *pse->m_State[i]->ExtractData<FEElasticMaterialPoint>();
						mat3d& m = pt.Q;
						m.zero();
						m[0][0] = a.x; m[0][1] = b.x; m[0][2] = c.x;
						m[1][0] = a.y; m[1][1] = b.y; m[1][2] = c.y;
						m[2][0] = a.z; m[2][1] = b.z; m[2][2] = c.z;
					}
				}
			}
			else if (tag == "mat_axis")
			{
				vec3d a, d;

				++tag;
				do
				{
					if (tag == "a") tag.value(a);
					else if (tag == "d") tag.value(d);
					else throw XMLReader::InvalidTag(tag);

					++tag;
				}
				while (!tag.isend());

				vec3d c = a^d;
				vec3d b = c^a;

				// normalize
				a.unit();
				b.unit();
				c.unit();

				// assign to element
				FESolidElement* pbe = dynamic_cast<FESolidElement*> (pe);
				FEShellElement* pse = dynamic_cast<FEShellElement*> (pe);
				if (pbe)
				{
					for (int i=0; i<pbe->GaussPoints(); ++i)
					{
						FEElasticMaterialPoint& pt = *pbe->m_State[i]->ExtractData<FEElasticMaterialPoint>();
						mat3d& m = pt.Q;
						m.zero();
						m[0][0] = a.x; m[0][1] = b.x; m[0][2] = c.x;
						m[1][0] = a.y; m[1][1] = b.y; m[1][2] = c.y;
						m[2][0] = a.z; m[2][1] = b.z; m[2][2] = c.z;
					}
				}
				if (pse)
				{
					for (int i=0; i<pse->GaussPoints(); ++i)
					{
						FEElasticMaterialPoint& pt = *pse->m_State[i]->ExtractData<FEElasticMaterialPoint>();
						mat3d& m = pt.Q;
						m.zero();
						m[0][0] = a.x; m[0][1] = b.x; m[0][2] = c.x;
						m[1][0] = a.y; m[1][1] = b.y; m[1][2] = c.y;
						m[2][0] = a.z; m[2][1] = b.z; m[2][2] = c.z;
					}
				}
			}
			else if (tag == "thickness")
			{
				FEShellElement* pse = dynamic_cast<FEShellElement*> (pe);
				if (pse == 0) throw XMLReader::InvalidTag(tag);

				// read shell thickness
				tag.value(&pse->m_h0[0],pse->Nodes());
			}
			else if (tag == "area")
			{
				FETrussElement* pt = dynamic_cast<FETrussElement*>(pe);
				if (pt == 0) throw XMLReader::InvalidTag(tag);

				// read truss area
				tag.value(pt->m_a0);
			}
			else throw XMLReader::InvalidTag(tag);
			++tag;
		}
		while (!tag.isend());

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
//! Reads the Geometry::Groups section of the FEBio input file

void FEBioGeometrySection::ParseNodeSetSection(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh &mesh = fem.m_mesh;

	// get the name attribute
	const char* szname = tag.AttributeValue("name");

	// read the list of indices
	vector<int> l;
	m_pim->ReadList(tag, l);
	assert(!l.empty());

	// create a new node set
	FENodeSet* pns = new FENodeSet(&mesh);
	pns->SetName(szname);

	// assign indices to node set
	int N = l.size();
	pns->create(N);
	for (int i=0; i<N; ++i) (*pns)[i] = l[i] - 1;

	// add the nodeset to the mesh
	mesh.AddNodeSet(pns);
}

//---------------------------------------------------------------------------------
// parse a surface section for contact definitions
//
bool FEBioBoundarySection::ParseSurfaceSection(XMLTag &tag, FESurface& s, int nfmt)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;
	int NN = m.Nodes();

	// count nr of faces
	int faces = 0, N, nf[4];
	XMLTag t(tag); ++t;
	while (!t.isend()) { faces++; ++t; }

	// allocate storage for faces
	s.create(faces);

	// read faces
	++tag;
	for (int i=0; i<faces; ++i)
	{
		FESurfaceElement& el = s.Element(i);

		if (tag == "quad4") el.SetType(FE_NIQUAD);
		else if (tag == "tri3") el.SetType(FE_NITRI);
		else throw XMLReader::InvalidTag(tag);

		N = el.Nodes();

		if (nfmt == 0)
		{
			tag.value(nf, N);
			for (int j=0; j<N; ++j) 
			{
				int nid = nf[j]-1;
				if ((nid<0)||(nid>= NN)) throw XMLReader::InvalidValue(tag);
				el.m_node[j] = nid;
			}
		}
		else if (nfmt == 1)
		{
			tag.value(nf, 2);
			FEElement* pe = m.FindElementFromID(nf[0]);
			if (pe)
			{
				int ne[4];
				int nn = m.GetFace(*pe, nf[1]-1, ne);
				if (nn != N) throw XMLReader::InvalidValue(tag);
				for (int j=0; j<N; ++j) el.m_node[j] = ne[j];
				el.m_nelem = nf[0];
			}
			else throw XMLReader::InvalidValue(tag);
		}

		++tag;
	}
	return true;
}

//=============================================================================
//
//                       B O U N D A R Y   S E C T I O N
//
//=============================================================================
//!  Parses the boundary section from the xml file
//!
void FEBioBoundarySection::Parse(XMLTag& tag)
{
	// make sure this tag has children
	if (tag.isleaf()) return;

	++tag;
	do
	{
		if      (tag == "fix"                  ) ParseBCFix               (tag);
		else if (tag == "prescribe"            ) ParseBCPrescribe         (tag);
		else if (tag == "force"                ) ParseBCForce             (tag);
		else if (tag == "pressure"             ) ParseBCPressure          (tag);
		else if (tag == "traction"             ) ParseBCTraction          (tag);
		else if (tag == "normal_traction"      ) ParseBCPoroNormalTraction(tag);
		else if (tag == "fluidflux"            ) ParseBCFluidFlux         (tag);
		else if (tag == "soluteflux"           ) ParseBCSoluteFlux        (tag);
		else if (tag == "heatflux"             ) ParseBCHeatFlux          (tag);
		else if (tag == "contact"              ) ParseContactSection      (tag);
		else if (tag == "linear_constraint"    ) ParseConstraints         (tag);
		else if (tag == "spring"               ) ParseSpringSection       (tag);
		else throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCFix(XMLTag &tag)
{
	FEM& fem = *GetFEM();

	// make sure this section does not appear in a step section
	if (m_pim->m_nsteps != 0) throw XMLReader::InvalidTag(tag);

	// see if a set is defined
	const char* szset = tag.AttributeValue("set", true);
	if (szset)
	{
		// read the set
		FEMesh& mesh = fem.m_mesh;
		FENodeSet* ps = mesh.FindNodeSet(szset);
		if (ps == 0) throw XMLReader::InvalidAttributeValue(tag, "set", szset);

		// get the bc attribute
		const char* sz = tag.AttributeValue("bc");

		// Make sure this is a leaf
		if (tag.isleaf() == false) throw XMLReader::InvalidValue(tag);

		// loop over all nodes in the nodeset
		FENodeSet& s = *ps;
		int N = s.size();
		for (int i=0; i<N; ++i)
		{
			FENode& node = mesh.Node(s[i]);
			if      (strcmp(sz, "x"  ) == 0) { node.m_ID[DOF_X] = -1; }
			else if (strcmp(sz, "y"  ) == 0) { node.m_ID[DOF_Y] = -1; }
			else if (strcmp(sz, "z"  ) == 0) { node.m_ID[DOF_Z] = -1; }
			else if (strcmp(sz, "xy" ) == 0) { node.m_ID[DOF_X] = node.m_ID[DOF_Y] = -1; }
			else if (strcmp(sz, "yz" ) == 0) { node.m_ID[DOF_Y] = node.m_ID[DOF_Z] = -1; }
			else if (strcmp(sz, "xz" ) == 0) { node.m_ID[DOF_X] = node.m_ID[DOF_Z] = -1; }
			else if (strcmp(sz, "xyz") == 0) { node.m_ID[DOF_X] = node.m_ID[DOF_Y] = node.m_ID[DOF_Z] = -1; }
			else if (strcmp(sz, "p"  ) == 0) { node.m_ID[DOF_P] = -1; }
			else if (strcmp(sz, "u"  ) == 0) { node.m_ID[DOF_U] = -1; }
			else if (strcmp(sz, "v"  ) == 0) { node.m_ID[DOF_V] = -1; }
			else if (strcmp(sz, "w"  ) == 0) { node.m_ID[DOF_W] = -1; }
			else if (strcmp(sz, "uv" ) == 0) { node.m_ID[DOF_U] = node.m_ID[DOF_V] = -1; }
			else if (strcmp(sz, "vw" ) == 0) { node.m_ID[DOF_V] = node.m_ID[DOF_W] = -1; }
			else if (strcmp(sz, "uw" ) == 0) { node.m_ID[DOF_U] = node.m_ID[DOF_W] = -1; }
			else if (strcmp(sz, "uvw") == 0) { node.m_ID[DOF_U] = node.m_ID[DOF_V] = node.m_ID[DOF_W] = -1; }
			else if (strcmp(sz, "t"  ) == 0) { node.m_ID[DOF_T] = -1; }
			else if (strcmp(sz, "c"  ) == 0) { node.m_ID[DOF_C] = -1; }
			else throw XMLReader::InvalidAttributeValue(tag, "bc", sz);
		}
	}
	else
	{
		// Read the fixed nodes
		++tag;
		do
		{
			int n = atoi(tag.AttributeValue("id"))-1;
			FENode& node = fem.m_mesh.Node(n);
			const char* sz = tag.AttributeValue("bc");
			if      (strcmp(sz, "x") == 0) node.m_ID[DOF_X] = -1;
			else if (strcmp(sz, "y") == 0) node.m_ID[DOF_Y] = -1;
			else if (strcmp(sz, "z") == 0) node.m_ID[DOF_Z] = -1;
			else if (strcmp(sz, "xy") == 0) { node.m_ID[DOF_X] = node.m_ID[DOF_Y] = -1; }
			else if (strcmp(sz, "yz") == 0) { node.m_ID[DOF_Y] = node.m_ID[DOF_Z] = -1; }
			else if (strcmp(sz, "xz") == 0) { node.m_ID[DOF_X] = node.m_ID[DOF_Z] = -1; }
			else if (strcmp(sz, "xyz") == 0) { node.m_ID[DOF_X] = node.m_ID[DOF_Y] = node.m_ID[DOF_Z] = -1; }
			else if (strcmp(sz, "p") == 0) { node.m_ID[DOF_P] = -1; }
			else if (strcmp(sz, "u") == 0) node.m_ID[DOF_U] = -1;
			else if (strcmp(sz, "v") == 0) node.m_ID[DOF_V] = -1;
			else if (strcmp(sz, "w") == 0) node.m_ID[DOF_W] = -1;
			else if (strcmp(sz, "uv") == 0) { node.m_ID[DOF_U] = node.m_ID[DOF_V] = -1; }
			else if (strcmp(sz, "vw") == 0) { node.m_ID[DOF_V] = node.m_ID[DOF_W] = -1; }
			else if (strcmp(sz, "uw") == 0) { node.m_ID[DOF_U] = node.m_ID[DOF_W] = -1; }
			else if (strcmp(sz, "uvw") == 0) { node.m_ID[DOF_U] = node.m_ID[DOF_V] = node.m_ID[DOF_W] = -1; }
			else if (strcmp(sz, "t") == 0) node.m_ID[DOF_T] = -1;
			else if (strcmp(sz, "c") == 0) { node.m_ID[DOF_C] = -1; }
			else throw XMLReader::InvalidAttributeValue(tag, "bc", sz);
			++tag;
		}
		while (!tag.isend());
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCPrescribe(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;

	int nversion = m_pim->Version();

	if (nversion >= 0x0200)
	{
		// count how many prescibed nodes there are
		int ndis = 0;
		XMLTag t(tag); ++t;
		while (!t.isend()) { ndis++; ++t; }

		// determine whether prescribed BC is relative or absolute
		bool br = false;
		const char* sztype = tag.AttributeValue("type",true);
		if (sztype && strcmp(sztype, "relative") == 0) br = true;

		// get the BC
		int bc = -1;
		const char* sz = tag.AttributeValue("bc");
		if      (strcmp(sz, "x") == 0) bc = DOF_X;
		else if (strcmp(sz, "y") == 0) bc = DOF_Y;
		else if (strcmp(sz, "z") == 0) bc = DOF_Z;
		else if (strcmp(sz, "u") == 0) bc = DOF_U;
		else if (strcmp(sz, "v") == 0) bc = DOF_V;
		else if (strcmp(sz, "w") == 0) bc = DOF_W;
		else if (strcmp(sz, "p") == 0) bc = DOF_P;
		else if (strcmp(sz, "t") == 0) bc = DOF_T; 
		else if (strcmp(sz, "c") == 0) bc = DOF_C;
		else throw XMLReader::InvalidAttributeValue(tag, "bc", sz);

		// read the prescribed data
		++tag;
		for (int i=0; i<ndis; ++i)
		{
			// get the node ID
			int n = atoi(tag.AttributeValue("id"))-1, lc;

			// get the load curve number
			sz = tag.AttributeValue("lc", true);
			if (sz == 0) lc = 0;
			else lc = atoi(sz);

			// create a new BC
			FEPrescribedBC* pdc = new FEPrescribedBC;
			pdc->node = n;
			pdc->bc = bc;
			pdc->lc = lc;
			tag.value(pdc->s);
			pdc->br = br;
			fem.m_DC.push_back(pdc);

			// add this boundary condition to the current step
			if (m_pim->m_nsteps > 0)
			{
				GetStep()->AddBoundaryCondition(pdc);
				pdc->Deactivate();
			}
			++tag;
		}
	}
	else
	{
		// see if this tag defines a set
		const char* szset = tag.AttributeValue("set", true);
		if (szset)
		{
			// Find the set
			FENodeSet* ps = mesh.FindNodeSet(szset);
			if (ps == 0) throw XMLReader::InvalidAttributeValue(tag, "set", szset);

			// get the bc attribute
			const char* sz = tag.AttributeValue("bc");

			int bc;
			if      (strcmp(sz, "x") == 0) bc = DOF_X;
			else if (strcmp(sz, "y") == 0) bc = DOF_Y;
			else if (strcmp(sz, "z") == 0) bc = DOF_Z;
			else if (strcmp(sz, "u") == 0) bc = DOF_U;
			else if (strcmp(sz, "v") == 0) bc = DOF_V;
			else if (strcmp(sz, "w") == 0) bc = DOF_W;
			else if (strcmp(sz, "p") == 0) bc = DOF_P;
			else if (strcmp(sz, "t") == 0) bc = DOF_T; 
			else if (strcmp(sz, "c") == 0) bc = DOF_C;
			else throw XMLReader::InvalidAttributeValue(tag, "bc", sz);

			// get the lc attribute
			int lc;
			sz = tag.AttributeValue("lc", true);
			if (sz == 0) lc = 0;
			else lc = atoi(sz);

			// make sure this tag is a leaf
			if (tag.isleaf() == false) throw XMLReader::InvalidValue(tag);

			// get the scale factor
			double s = 1;
			tag.value(s);

			// loop over all nodes in the nodeset
			FENodeSet& ns = *ps;
			int N = ns.size();
			for (int i=0; i<N; ++i)
			{
				FEPrescribedBC* pdc = new FEPrescribedBC;
				pdc->node = ns[i];
				pdc->bc = bc;
				pdc->lc = lc;
				pdc->s = s;
				fem.m_DC.push_back(pdc);

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					GetStep()->AddBoundaryCondition(pdc);
					pdc->Deactivate();
				}
			}
		}
		else
		{
			// count how many prescibed nodes there are
			int ndis = 0;
			XMLTag t(tag); ++t;
			while (!t.isend()) { ndis++; ++t; }

			// determine whether prescribed BC is relative or absolute
			bool br = false;
			const char* sztype = tag.AttributeValue("type",true);
			if (sztype && strcmp(sztype, "relative") == 0) br = true;

			// read the prescribed data
			++tag;
			for (int i=0; i<ndis; ++i)
			{
				int n = atoi(tag.AttributeValue("id"))-1, bc, lc;
				const char* sz = tag.AttributeValue("bc");

				if      (strcmp(sz, "x") == 0) bc = DOF_X;
				else if (strcmp(sz, "y") == 0) bc = DOF_Y;
				else if (strcmp(sz, "z") == 0) bc = DOF_Z;
				else if (strcmp(sz, "u") == 0) bc = DOF_U;
				else if (strcmp(sz, "v") == 0) bc = DOF_V;
				else if (strcmp(sz, "w") == 0) bc = DOF_W;
				else if (strcmp(sz, "p") == 0) bc = DOF_P;
				else if (strcmp(sz, "t") == 0) bc = DOF_T; 
				else if (strcmp(sz, "c") == 0) bc = DOF_C;
				else throw XMLReader::InvalidAttributeValue(tag, "bc", sz);

				sz = tag.AttributeValue("lc", true);
				if (sz == 0) lc = 0;
				else lc = atoi(sz);

				FEPrescribedBC* pdc = new FEPrescribedBC;
				pdc->node = n;
				pdc->bc = bc;
				pdc->lc = lc;
				tag.value(pdc->s);
				pdc->br = br;
				fem.m_DC.push_back(pdc);

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					GetStep()->AddBoundaryCondition(pdc);
					pdc->Deactivate();
				}
				++tag;
			}
		}
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCForce(XMLTag &tag)
{
	FEM& fem = *GetFEM();

	int nversion = m_pim->Version();
	if (nversion >= 0x0200)
	{
		// count how many nodal forces there are
		int ncnf = 0;
		XMLTag t(tag); ++t;
		while (!t.isend()) { ncnf++; ++t; }

		// get the bc
		int bc = -1;
		const char* sz = tag.AttributeValue("bc");
		if      (strcmp(sz, "x") == 0) bc = 0;
		else if (strcmp(sz, "y") == 0) bc = 1;
		else if (strcmp(sz, "z") == 0) bc = 2;
		else if (strcmp(sz, "p") == 0) bc = 6;
		else if (strcmp(sz, "t") == 0) bc = 10;
		else if (strcmp(sz, "c") == 0) bc = 11;
		else throw XMLReader::InvalidAttributeValue(tag, "bc", sz);

		// read the prescribed data
		++tag;
		for (int i=0; i<ncnf; ++i)
		{
			// get the nodal ID
			int n = atoi(tag.AttributeValue("id"))-1;

			// get the load curve
			sz = tag.AttributeValue("lc", true);
			int lc = (sz == 0? 0 : lc = atoi(sz));

			// create new nodal force
			FENodalForce* pfc = new FENodalForce;
			pfc->node = n;
			pfc->bc = bc;
			pfc->lc = lc;
			tag.value(pfc->s);
			fem.m_FC.push_back(pfc);

			// add this boundary condition to the current step
			if (m_pim->m_nsteps > 0)
			{
				GetStep()->AddBoundaryCondition(pfc);
				pfc->Deactivate();
			}

			++tag;
		}
	}
	else
	{
		// count how many nodal forces there are
		int ncnf = 0;
		XMLTag t(tag); ++t;
		while (!t.isend()) { ncnf++; ++t; }

		// read the prescribed data
		++tag;
		for (int i=0; i<ncnf; ++i)
		{
			int n = atoi(tag.AttributeValue("id"))-1, bc, lc;
			const char* sz = tag.AttributeValue("bc");

			if      (strcmp(sz, "x") == 0) bc = 0;
			else if (strcmp(sz, "y") == 0) bc = 1;
			else if (strcmp(sz, "z") == 0) bc = 2;
			else if (strcmp(sz, "p") == 0) bc = 6;
			else if (strcmp(sz, "t") == 0) bc = 10;
			else if (strcmp(sz, "c") == 0) bc = 11;
			else throw XMLReader::InvalidAttributeValue(tag, "bc", sz);

			sz = tag.AttributeValue("lc", true);
			if (sz == 0) lc = 0;
			else lc = atoi(sz);

			FENodalForce* pfc = new FENodalForce;
			pfc->node = n;
			pfc->bc = bc;
			pfc->lc = lc;
			tag.value(pfc->s);
			fem.m_FC.push_back(pfc);

			// add this boundary condition to the current step
			if (m_pim->m_nsteps > 0)
			{
				GetStep()->AddBoundaryCondition(pfc);
				pfc->Deactivate();
			}

			++tag;
		}
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCPressure(XMLTag& tag)
{
	FEM& fem = *GetFEM();

	const char* sz;
	bool blinear = false;
	sz = tag.AttributeValue("type", true);
	if (sz)
	{
		if (strcmp(sz, "linear") == 0) blinear = true;
		else if (strcmp(sz, "nonlinear") == 0) blinear = false;
		else throw XMLReader::InvalidAttributeValue(tag, "type", sz);
	}

	// count how many pressure cards there are
	int npr = 0;
	XMLTag t(tag); ++t;
	while (!t.isend()) { npr++; ++t; }

	// create a new surface
	FESurface* psurf = new FESurface(&fem.m_mesh);
	psurf->create(npr);
	fem.m_mesh.AddSurface(psurf);

	// allocate pressure data
	FEPressureLoad* ps = new FEPressureLoad(psurf, blinear);
	ps->create(npr);
	fem.m_SL.push_back(ps);

	// read the pressure data
	++tag;
	int nf[4], N;
	double s;
	for (int i=0; i<npr; ++i)
	{
		FEPressureLoad::LOAD& pc = ps->PressureLoad(i);
		FESurfaceElement& el = psurf->Element(i);

		sz = tag.AttributeValue("lc", true);
		if (sz) pc.lc = atoi(sz); else pc.lc = 0;

		s  = atof(tag.AttributeValue("scale"));
		pc.s[0] = pc.s[1] = pc.s[2] = pc.s[3] = s;

		if (tag == "quad4") el.SetType(FE_QUAD);
		else if (tag == "tri3") el.SetType(FE_TRI);
		else throw XMLReader::InvalidTag(tag);

		N = el.Nodes();
		tag.value(nf, N);
		for (int j=0; j<N; ++j) el.m_node[j] = nf[j]-1;

		++tag;
	}

	// add this boundary condition to the current step
	if (m_pim->m_nsteps > 0)
	{
		GetStep()->AddBoundaryCondition(ps);
		ps->Deactivate();
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCTraction(XMLTag &tag)
{
	FEM& fem = *GetFEM();

	const char* sz;

	// count how many traction cards there are
	int ntc = 0;
	XMLTag t(tag); ++t;
	while (!t.isend()) { ntc++; ++t; }

	// create a new surface
	FESurface* psurf = new FESurface(&fem.m_mesh);
	psurf->create(ntc);
	fem.m_mesh.AddSurface(psurf);

	// allocate traction data
	FETractionLoad* pt = new FETractionLoad(psurf);
	fem.m_SL.push_back(pt);
	pt->create(ntc);

	// read the traction data
	++tag;
	int nf[4], N;
	vec3d s;
	for (int i=0; i<ntc; ++i)
	{
		FETractionLoad::LOAD& tc = pt->TractionLoad(i);
		FESurfaceElement& el = psurf->Element(i);

		sz = tag.AttributeValue("lc", true);
		if (sz) tc.lc = atoi(sz); else tc.lc = 0;

		s.x  = atof(tag.AttributeValue("tx"));
		s.y  = atof(tag.AttributeValue("ty"));
		s.z  = atof(tag.AttributeValue("tz"));

		tc.s[0] = tc.s[1] = tc.s[2] = tc.s[3] = s;

		if (tag == "quad4") el.SetType(FE_QUAD);
		else if (tag == "tri3") el.SetType(FE_TRI);
		else throw XMLReader::InvalidTag(tag);

		N = el.Nodes();
		tag.value(nf, N);
		for (int j=0; j<N; ++j) el.m_node[j] = nf[j]-1;

		++tag;
	}

	// add this boundary condition to the current step
	if (m_pim->m_nsteps > 0)
	{
		GetStep()->AddBoundaryCondition(pt);
		pt->Deactivate();
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCPoroNormalTraction(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	
	const char* sz;
	bool blinear = false;
	sz = tag.AttributeValue("type", true);
	if (sz)
	{
		if (strcmp(sz, "linear") == 0) blinear = true;
		else if (strcmp(sz, "nonlinear") == 0) blinear = false;
		else throw XMLReader::InvalidAttributeValue(tag, "type", sz);
	}
	
	bool beffective = false;
	sz = tag.AttributeValue("traction", true);
	if (sz)
	{
		if (strcmp(sz, "effective") == 0) beffective = true;
		else if ((strcmp(sz, "total") == 0) || (strcmp(sz, "mixture") == 0)) beffective = false;
		else throw XMLReader::InvalidAttributeValue(tag, "traction", sz);
	}
	
	// count how many normal traction cards there are
	int npr = 0;
	XMLTag t(tag); ++t;
	while (!t.isend()) { npr++; ++t; }

	// create a new surface
	FESurface* psurf = new FESurface(&fem.m_mesh);
	psurf->create(npr);
	fem.m_mesh.AddSurface(psurf);
	
	// allocate normal traction data
	FEPoroNormalTraction* ps = new FEPoroNormalTraction(psurf, blinear, beffective);
	ps->create(npr);
	fem.m_SL.push_back(ps);
	
	// read the normal traction data
	++tag;
	int nf[4], N;
	double s;
	for (int i=0; i<npr; ++i)
	{
		FEPoroNormalTraction::LOAD& pc = ps->NormalTraction(i);
		FESurfaceElement& el = psurf->Element(i);
		
		sz = tag.AttributeValue("lc", true);
		if (sz) pc.lc = atoi(sz); else pc.lc = 0;
		
		s  = atof(tag.AttributeValue("scale"));
		pc.s[0] = pc.s[1] = pc.s[2] = pc.s[3] = s;
		
		if (tag == "quad4") el.SetType(FE_QUAD);
		else if (tag == "tri3") el.SetType(FE_TRI);
		else throw XMLReader::InvalidTag(tag);
		
		N = el.Nodes();
		tag.value(nf, N);
		for (int j=0; j<N; ++j) el.m_node[j] = nf[j]-1;
		
		++tag;
	}

	// add this boundary condition to the current step
	if (m_pim->m_nsteps > 0)
	{
		GetStep()->AddBoundaryCondition(ps);
		ps->Deactivate();
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCFluidFlux(XMLTag &tag)
{
	FEM& fem = *GetFEM();

	const char* sz;
	bool blinear = false;
	sz = tag.AttributeValue("type", true);
	if (sz)
	{
		if (strcmp(sz, "linear") == 0) blinear = true;
		else if (strcmp(sz, "nonlinear") == 0) blinear = false;
		else throw XMLReader::InvalidAttributeValue(tag, "type", sz);
	}
	
	bool bmixture = false;
	sz = tag.AttributeValue("flux", true);
	if (sz)
	{
		if (strcmp(sz, "mixture") == 0) bmixture = true;
		else if (strcmp(sz, "fluid") == 0) bmixture = false;
		else throw XMLReader::InvalidAttributeValue(tag, "flux", sz);
	}
	
	// count how many fluid flux cards there are
	int nfr = 0;
	XMLTag t(tag); ++t;
	while (!t.isend()) { nfr++; ++t; }

	// create a new surface
	FESurface* psurf = new FESurface(&fem.m_mesh);
	psurf->create(nfr);
	fem.m_mesh.AddSurface(psurf);
	
	// allocate fluid flux data
	FEFluidFlux* pfs = new FEFluidFlux(psurf, blinear, bmixture);
	pfs->create(nfr);
	fem.m_SL.push_back(pfs);
	
	// read the fluid flux data
	++tag;
	int nf[4], N;
	double s;
	for (int i=0; i<nfr; ++i)
	{
		FEFluidFlux::LOAD& fc = pfs->FluidFlux(i);
		FESurfaceElement& el = psurf->Element(i);
		
		sz = tag.AttributeValue("lc", true);
		if (sz) fc.lc = atoi(sz); else fc.lc = 0;
		
		s  = atof(tag.AttributeValue("scale"));
		fc.s[0] = fc.s[1] = fc.s[2] = fc.s[3] = s;
		
		if (tag == "quad4") el.SetType(FE_QUAD);
		else if (tag == "tri3") el.SetType(FE_TRI);
		else throw XMLReader::InvalidTag(tag);
		
		N = el.Nodes();
		tag.value(nf, N);
		for (int j=0; j<N; ++j) el.m_node[j] = nf[j]-1;
		
		++tag;
	}

	// add this boundary condition to the current step
	if (m_pim->m_nsteps > 0)
	{
		GetStep()->AddBoundaryCondition(pfs);
		pfs->Deactivate();
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCSoluteFlux(XMLTag &tag)
{
	FEM& fem = *GetFEM();
	
	const char* sz;
	bool blinear = false;
	sz = tag.AttributeValue("type", true);
	if (sz)
	{
		if (strcmp(sz, "linear") == 0) blinear = true;
		else if (strcmp(sz, "nonlinear") == 0) blinear = false;
		else throw XMLReader::InvalidAttributeValue(tag, "type", sz);
	}
	
	// count how many fluid flux cards there are
	int nfr = 0;
	XMLTag t(tag); ++t;
	while (!t.isend()) { nfr++; ++t; }

	// create a new surface
	FESurface* psurf = new FESurface(&fem.m_mesh);
	psurf->create(nfr);
	fem.m_mesh.AddSurface(psurf);
	
	// allocate fluid flux data
	FESoluteFlux* pfs = new FESoluteFlux(psurf, blinear);
	pfs->create(nfr);
	fem.m_SL.push_back(pfs);
	
	// read the fluid flux data
	++tag;
	int nf[4], N;
	double s;
	for (int i=0; i<nfr; ++i)
	{
		FESoluteFlux::LOAD& fc = pfs->SoluteFlux(i);
		FESurfaceElement& el = psurf->Element(i);
		
		sz = tag.AttributeValue("lc", true);
		if (sz) fc.lc = atoi(sz); else fc.lc = 0;
		
		s  = atof(tag.AttributeValue("scale"));
		fc.s[0] = fc.s[1] = fc.s[2] = fc.s[3] = s;
		
		if (tag == "quad4") el.SetType(FE_QUAD);
		else if (tag == "tri3") el.SetType(FE_TRI);
		else throw XMLReader::InvalidTag(tag);
		
		N = el.Nodes();
		tag.value(nf, N);
		for (int j=0; j<N; ++j) el.m_node[j] = nf[j]-1;
		
		++tag;
	}

	// add this boundary condition to the current step
	if (m_pim->m_nsteps > 0)
	{
		GetStep()->AddBoundaryCondition(pfs);
		pfs->Deactivate();
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseBCHeatFlux(XMLTag& tag)
{
	FEM& fem = *GetFEM();

	// count how many heatflux cards there are
	int npr = 0;
	XMLTag t(tag); ++t;
	while (!t.isend()) { npr++; ++t; }

	// create a new surface
	FESurface* psurf = new FESurface(&fem.m_mesh);
	psurf->create(npr);
	fem.m_mesh.AddSurface(psurf);

	// allocate flux data
	FEHeatFlux* ph = new FEHeatFlux(psurf);
	ph->create(npr);
	fem.m_SL.push_back(ph);

	const char* sz;

	// read the flux data
	++tag;
	int nf[4], N;
	double s;
	for (int i=0; i<npr; ++i)
	{
		FEHeatFlux::LOAD& pc = ph->HeatFlux(i);
		FESurfaceElement& el = psurf->Element(i);

		sz = tag.AttributeValue("lc", true);
		if (sz) pc.lc = atoi(sz); else pc.lc = 0;

		s  = atof(tag.AttributeValue("scale"));
		pc.s[0] = pc.s[1] = pc.s[2] = pc.s[3] = s;

		if (tag == "quad4") el.SetType(FE_QUAD);
		else if (tag == "tri3") el.SetType(FE_TRI);
		else throw XMLReader::InvalidTag(tag);

		N = el.Nodes();
		tag.value(nf, N);
		for (int j=0; j<N; ++j) el.m_node[j] = nf[j]-1;

		++tag;
	}

	// add this boundary condition to the current step
	if (m_pim->m_nsteps > 0)
	{
		GetStep()->AddBoundaryCondition(ph);
		ph->Deactivate();
	}
}

//-----------------------------------------------------------------------------
void FEBioBoundarySection::ParseSpringSection(XMLTag &tag)
{
	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;

	// determine the spring type
	FEDiscreteMaterial* pm = 0;
	const char* szt = tag.AttributeValue("type", true);
	if (szt)
	{
		if (strcmp(szt, "linear") == 0) pm = new FELinearSpring;
		else if (strcmp(szt, "tension-only linear") == 0) pm = new FETensionOnlyLinearSpring;
		else if (strcmp(szt, "nonlinear") == 0) pm = new FENonLinearSpring;
	}
	else pm = new FELinearSpring;

	// create a new spring "domain"
	FEDiscreteSpringDomain* pd = new FEDiscreteSpringDomain(&mesh, pm);
	mesh.AddDomain(pd);

	pd->create(1);
	FEDiscreteElement& de = dynamic_cast<FEDiscreteElement&>(pd->ElementRef(0));
	de.SetType(FE_DISCRETE);
	
	// add a new material for each spring
	fem.AddMaterial(pm);
	pm->SetID(fem.Materials());
	de.SetMatID(fem.Materials()-1);

	int n[2];

	// read spring discrete elements
	++tag;
	do
	{
		if (tag == "node")
		{
			tag.value(n, 2);
			de.m_node[0] = n[0]-1;
			de.m_node[1] = n[1]-1;
		}
		else if (tag == "E") 
		{
			if (dynamic_cast<FELinearSpring*>(pm)) tag.value((dynamic_cast<FELinearSpring*>(pm))->m_E);
			else if (dynamic_cast<FETensionOnlyLinearSpring*>(pm)) tag.value((dynamic_cast<FETensionOnlyLinearSpring*>(pm))->m_E);
			else throw XMLReader::InvalidTag(tag);
		}
		else if (tag == "force")
		{
			if (dynamic_cast<FENonLinearSpring*>(pm))
			{
				FENonLinearSpring* ps = dynamic_cast<FENonLinearSpring*>(pm);
				tag.value(ps->m_F);
				const char* szl = tag.AttributeValue("lc");
				int lc = atoi(szl);
				ps->m_nlc = lc;
			}
			else throw XMLReader::InvalidTag(tag);
		}
		else throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());

	pd->InitMaterialPointData();
}

//-----------------------------------------------------------------------------
//! Parse the linear constraints section of the xml input file
//! This section is a subsection of the Boundary section

void FEBioBoundarySection::ParseConstraints(XMLTag& tag)
{
	FEM& fem = *GetFEM();

	// make sure there is a constraint defined
	if (tag.isleaf()) return;

	// read the master node
	FELinearConstraint LC;
	int node;
	tag.AttributeValue("node", node);
	LC.master.node = node-1;

	const char* szbc = tag.AttributeValue("bc");
	if      (strcmp(szbc, "x") == 0) LC.master.bc = 0;
	else if (strcmp(szbc, "y") == 0) LC.master.bc = 1;
	else if (strcmp(szbc, "z") == 0) LC.master.bc = 2;
	else throw XMLReader::InvalidAttributeValue(tag, "bc", szbc);

	// we must deactive the master dof
	// so that it does not get assigned an equation
	fem.m_mesh.Node(node-1).m_ID[LC.master.bc] = -1;

	// read the slave nodes
	++tag;
	do
	{
		FELinearConstraint::SlaveDOF dof;
		if (tag == "node")
		{
			tag.value(dof.val);
			tag.AttributeValue("id", node);
			dof.node = node - 1;

			const char* szbc = tag.AttributeValue("bc");
			if      (strcmp(szbc, "x") == 0) dof.bc = 0;
			else if (strcmp(szbc, "y") == 0) dof.bc = 1;
			else if (strcmp(szbc, "z") == 0) dof.bc = 2;
			else throw XMLReader::InvalidAttributeValue(tag, "bc", szbc);

			LC.slave.push_back(dof);
		}
		else throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());

	// add the linear constraint to the system
	fem.m_LinC.push_back(LC);
}


//-----------------------------------------------------------------------------
//! Parses the contact section of the xml input file
//! The contact section is a subsection of the boundary section

void FEBioBoundarySection::ParseContactSection(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	// make sure that the version is 1.x
	int nversion = m_pim->Version();
	if (nversion >= 0x0200) throw XMLReader::InvalidTag(tag);

	const char* szt = tag.AttributeValue("type");

	if (strcmp(szt, "sliding_with_gaps") == 0)
	{
		// --- S L I D I N G   W I T H   G A P S ---

		FESlidingInterface* ps = new FESlidingInterface(&fem);
		fem.AddContactInterface(ps);

		FEParameterList& pl = ps->GetParameterList();

		++tag;
		do
		{
			// read parameters
			if (m_pim->ReadParameter(tag, pl) == false)
			{
				if (tag == "surface")
				{
					const char* sztype = tag.AttributeValue("type");
					int ntype;
					if (strcmp(sztype, "master") == 0) ntype = 1;
					else if (strcmp(sztype, "slave") == 0) ntype = 2;

					FESlidingSurface& s = (ntype == 1? ps->m_ms : ps->m_ss);
					m.AddSurface(&s);

					int nfmt = 0;
					const char* szfmt = tag.AttributeValue("format", true);
					if (szfmt)
					{
						if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
						else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
					}

					// read the surface section
					ParseSurfaceSection(tag, s, nfmt);
				}
				else throw XMLReader::InvalidTag(tag);
			}

			++tag;
		}
		while (!tag.isend());
	}
	else if (strcmp(szt, "facet-to-facet sliding") == 0)
	{
		// --- F A C E T   T O   F A C E T   S L I D I N G ---

		FEFacet2FacetSliding* ps = new FEFacet2FacetSliding(&fem);
		fem.AddContactInterface(ps);

		FEParameterList& pl = ps->GetParameterList();

		++tag;
		do
		{
			// read parameters
			if (m_pim->ReadParameter(tag, pl) == false)
			{
				if (tag == "surface")
				{
					const char* sztype = tag.AttributeValue("type");
					int ntype;
					if (strcmp(sztype, "master") == 0) ntype = 1;
					else if (strcmp(sztype, "slave") == 0) ntype = 2;

					FEFacetSlidingSurface& s = (ntype == 1? ps->m_ms : ps->m_ss);
					m.AddSurface(&s);

					int nfmt = 0;
					const char* szfmt = tag.AttributeValue("format", true);
					if (szfmt)
					{
						if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
						else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
					}

					// read the surface section
					ParseSurfaceSection(tag, s, nfmt);

					// currently the element types are automatically set to FE_NIQUAD or FE_NITRI
					// so we have to modify those elements to FE_QUAD and FE_TRI
					// TODO: we need a better way of doing this!
					for (int i=0; i<s.Elements(); ++i)
					{
						FESurfaceElement& e = s.Element(i);
						if (e.Nodes() == 4) e.SetType(FE_QUAD); 
						else e.SetType(FE_TRI);
					}
				}
				else throw XMLReader::InvalidTag(tag);
			}

			++tag;
		}
		while (!tag.isend());
	}
	else if (strcmp(szt, "sliding2") == 0)
	{
		// --- S L I D I N G   I N T E R F A C E   2 ---
		FESlidingInterface2* ps = new FESlidingInterface2(&fem);
		fem.AddContactInterface(ps);

		FEParameterList& pl = ps->GetParameterList();

		++tag;
		do
		{
			// read parameters
			if (m_pim->ReadParameter(tag, pl) == false)
			{
				if (tag == "surface")
				{
					const char* sztype = tag.AttributeValue("type");
					int ntype;
					if (strcmp(sztype, "master") == 0) ntype = 1;
					else if (strcmp(sztype, "slave") == 0) ntype = 2;

					FESlidingSurface2& s = (ntype == 1? ps->m_ms : ps->m_ss);
					m.AddSurface(&s);

					int nfmt = 0;
					const char* szfmt = tag.AttributeValue("format", true);
					if (szfmt)
					{
						if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
						else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
					}

					// read the surface section
					ParseSurfaceSection(tag, s, nfmt);

					// currently the element types are automatically set to FE_NIQUAD or FE_NITRI
					// For this type of contact we want gaussian quadrature,
					// so we have to modify those elements to FE_QUAD and FE_TRI
					// TODO: we need a better way of doing this!
					for (int i=0; i<s.Elements(); ++i)
					{
						FESurfaceElement& e = s.Element(i);
						if (e.Nodes() == 4) e.SetType(FE_QUAD); 
						else e.SetType(FE_TRI);
					}
				}
				else throw XMLReader::InvalidTag(tag);
			}

			++tag;
		}
		while (!tag.isend());
	}
	else if (strcmp(szt, "sliding3") == 0)
	{
		// --- S L I D I N G   I N T E R F A C E   3 ---
		FESlidingInterface3* ps = new FESlidingInterface3(&fem);
		fem.AddContactInterface(ps);

		FEParameterList& pl = ps->GetParameterList();
		
		++tag;
		do
		{
			// read parameters
			if (m_pim->ReadParameter(tag, pl) == false)
			{
				if (tag == "surface")
				{
					const char* sztype = tag.AttributeValue("type");
					int ntype;
					if (strcmp(sztype, "master") == 0) ntype = 1;
					else if (strcmp(sztype, "slave") == 0) ntype = 2;
					
					FESlidingSurface3& s = (ntype == 1? ps->m_ms : ps->m_ss);
					m.AddSurface(&s);
					
					int nfmt = 0;
					const char* szfmt = tag.AttributeValue("format", true);
					if (szfmt)
					{
						if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
						else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
					}
					
					// read the surface section
					ParseSurfaceSection(tag, s, nfmt);
					
					// currently the element types are automatically set to FE_NIQUAD or FE_NITRI
					// For this type of contact we want gaussian quadrature,
					// so we have to modify those elements to FE_QUAD and FE_TRI
					// TODO: we need a better way of doing this!
					for (int i=0; i<s.Elements(); ++i)
					{
						FESurfaceElement& e = s.Element(i);
						if (e.Nodes() == 4) e.SetType(FE_QUAD); 
						else e.SetType(FE_TRI);
					}
				}
				else throw XMLReader::InvalidTag(tag);
			}
			
			++tag;
		}
		while (!tag.isend());
	}
	else if (strcmp(szt, "tied") == 0)
	{
		// --- T I E D   C O N T A C T  ---

		FETiedInterface* ps = new FETiedInterface(&fem);
		fem.AddContactInterface(ps);

		FEParameterList& pl = ps->GetParameterList();

		++tag;
		do
		{
			if (m_pim->ReadParameter(tag, pl) == false)
			{
				if (tag == "surface")
				{
					const char* sztype = tag.AttributeValue("type");
					int ntype;
					if (strcmp(sztype, "master") == 0) ntype = 1;
					else if (strcmp(sztype, "slave") == 0) ntype = 2;

					FETiedContactSurface& s = (ntype == 1? ps->ms : ps->ss);
					m.AddSurface(&s);

					int nfmt = 0;
					const char* szfmt = tag.AttributeValue("format", true);
					if (szfmt)
					{
						if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
						else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
					}

					// read the surface section
					ParseSurfaceSection(tag, s, nfmt);
				}
				else throw XMLReader::InvalidTag(tag);
			}

			++tag;
		}
		while (!tag.isend());
	}
	else if (strcmp(szt, "periodic boundary") == 0)
	{
		// --- P E R I O D I C   B O U N D A R Y  ---

		FEPeriodicBoundary* ps = new FEPeriodicBoundary(&fem);
		fem.AddContactInterface(ps);

		FEParameterList& pl = ps->GetParameterList();

		++tag;
		do
		{
			if (m_pim->ReadParameter(tag, pl) == false)
			{
				if (tag == "surface")
				{
					const char* sztype = tag.AttributeValue("type");
					int ntype;
					if (strcmp(sztype, "master") == 0) ntype = 1;
					else if (strcmp(sztype, "slave") == 0) ntype = 2;

					FEPeriodicSurface& s = (ntype == 1? ps->m_ms : ps->m_ss);
					m.AddSurface(&s);

					int nfmt = 0;
					const char* szfmt = tag.AttributeValue("format", true);
					if (szfmt)
					{
						if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
						else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
					}

					// read the surface section
					ParseSurfaceSection(tag, s, nfmt);
				}
				else throw XMLReader::InvalidTag(tag);
			}

			++tag;
		}
		while (!tag.isend());
	}
	else if (strcmp(szt, "surface constraint") == 0)
	{
		// --- S U R F A C E   C O N S T R A I N T ---

		FESurfaceConstraint* ps = new FESurfaceConstraint(&fem);
		fem.AddContactInterface(ps);

		FEParameterList& pl = ps->GetParameterList();

		++tag;
		do
		{
			if (m_pim->ReadParameter(tag, pl) == false)
			{
				if (tag == "surface")
				{
					const char* sztype = tag.AttributeValue("type");
					int ntype;
					if (strcmp(sztype, "master") == 0) ntype = 1;
					else if (strcmp(sztype, "slave") == 0) ntype = 2;

					FESurfaceConstraintSurface& s = (ntype == 1? ps->m_ms : ps->m_ss);
					m.AddSurface(&s);

					int nfmt = 0;
					const char* szfmt = tag.AttributeValue("format", true);
					if (szfmt)
					{
						if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
						else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
					}

					// read the surface section
					ParseSurfaceSection(tag, s, nfmt);
				}
				else throw XMLReader::InvalidTag(tag);
			}

			++tag;
		}
		while (!tag.isend());
	}	
	else if (strcmp(szt, "rigid_wall") == 0)
	{
		// --- R I G I D   W A L L   I N T E R F A C E ---

		FERigidWallInterface* ps = new FERigidWallInterface(&fem);
		fem.AddContactInterface(ps);

		FEParameterList& pl = ps->GetParameterList();

		++tag;
		do
		{
			if (m_pim->ReadParameter(tag, pl) == false)
			{
				if (tag == "plane")
				{
					ps->SetMasterSurface(new FEPlane(&fem));
					FEPlane& pl = dynamic_cast<FEPlane&>(*ps->m_mp);
					const char* sz = tag.AttributeValue("lc", true);
					if (sz)	pl.m_nplc = atoi(sz);

					double* a = pl.GetEquation();
					tag.value(a, 4);
				}
				else if (tag == "sphere")
				{
					ps->SetMasterSurface(new FERigidSphere(&fem));
					FERigidSphere& s = dynamic_cast<FERigidSphere&>(*ps->m_mp);
					++tag;
					do
					{
						if      (tag == "center") tag.value(s.m_rc);
						else if (tag == "radius") tag.value(s.m_R);
						else if (tag == "xtrans")
						{
							const char* szlc = tag.AttributeValue("lc");
							s.m_nplc[0] = atoi(szlc);
						}
						else if (tag == "ytrans")
						{
							const char* szlc = tag.AttributeValue("lc");
							s.m_nplc[1] = atoi(szlc);
						}
						else if (tag == "ztrans")
						{
							const char* szlc = tag.AttributeValue("lc");
							s.m_nplc[2] = atoi(szlc);
						}
						else throw XMLReader::InvalidTag(tag);
						++tag;
					}
					while (!tag.isend());
				}
				else if (tag == "surface")
				{
					FERigidWallSurface& s = ps->m_ss;

					int nfmt = 0;
					const char* szfmt = tag.AttributeValue("format", true);
					if (szfmt)
					{
						if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
						else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
					}

					// read the surface section
					ParseSurfaceSection(tag, s, nfmt);
				}
				else throw XMLReader::InvalidTag(tag);
			}
			++tag;
		}
		while (!tag.isend());
	}
	else if (strcmp(szt, "rigid") == 0)
	{
		// --- R I G I D   B O D Y   I N T E R F A C E ---

		// count how many rigid nodes there are
		int nrn= 0;
		XMLTag t(tag); ++t;
		while (!t.isend()) { nrn++; ++t; }

		++tag;
		int id, rb;
		for (int i=0; i<nrn; ++i)
		{
			id = atoi(tag.AttributeValue("id"))-1;
			rb = atoi(tag.AttributeValue("rb"))-1;

			FERigidNode* prn = new FERigidNode;

			prn->nid = id;
			prn->rid = rb;
			fem.m_RN.push_back(prn);

			if (m_pim->m_nsteps > 0)
			{
				GetStep()->AddBoundaryCondition(prn);
				prn->Deactivate();
			}

			++tag;
		}
	}
	else if (strcmp(szt, "rigid joint") == 0)
	{
		// --- R I G I D   J O I N T   I N T E R F A C E ---

		FERigidJoint* prj = new FERigidJoint(&fem);
		FEParameterList& pl = prj->GetParameterList();
		++tag;
		do
		{
			if (m_pim->ReadParameter(tag, pl) == false) throw XMLReader::InvalidTag(tag);
			++tag;
		}
		while (!tag.isend());
		prj->m_nRBa--;
		prj->m_nRBb--;
		fem.m_RJ.push_back(prj);
	}
	else if (strcmp(szt, "linear constraint") == 0)
	{
		FEM& fem = *GetFEM();

		// make sure there is a constraint defined
		if (tag.isleaf()) return;

		// create a new linear constraint manager
		FELinearConstraintSet* pLCS = new FELinearConstraintSet(&fem);
		fem.m_LCSet.push_back(pLCS);

		// read the linear constraints
		++tag;
		do
		{
			if (tag == "linear_constraint")
			{
				FEAugLagLinearConstraint* pLC = new FEAugLagLinearConstraint;

				FEAugLagLinearConstraint::DOF dof;
				++tag;
				do
				{
					if (tag == "node")
					{
						tag.value(dof.val);
						int node;
						tag.AttributeValue("id", node);
						dof.node = node - 1;

						const char* szbc = tag.AttributeValue("bc");
						if      (strcmp(szbc, "x") == 0) dof.bc = 0;
						else if (strcmp(szbc, "y") == 0) dof.bc = 1;
						else if (strcmp(szbc, "z") == 0) dof.bc = 2;
						else throw XMLReader::InvalidAttributeValue(tag, "bc", szbc);

						pLC->m_dof.push_back(dof);
					}
					else throw XMLReader::InvalidTag(tag);
					++tag;
				}
				while (!tag.isend());

				// add the linear constraint to the system
				pLCS->add(pLC);
			}
			else if (tag == "tol"    ) tag.value(pLCS->m_tol);
			else if (tag == "penalty") tag.value(pLCS->m_eps);
			else if (tag == "maxaug") tag.value(pLCS->m_naugmax);
			else throw XMLReader::InvalidTag(tag);
			++tag;
		}
		while (!tag.isend());
	}
	else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
}

//=============================================================================
//
//                       C O N T A C T   S E C T I O N
//
//=============================================================================
// Parse the Contact section (new in version 2.0)
void FEBioContactSection::Parse(XMLTag& tag)
{
	// make sure that the version is 2.x
	int nversion = m_pim->Version();
	if (nversion < 0x0200) throw XMLReader::InvalidTag(tag);

	// loop over tags
	++tag;
	do
	{
		if (tag == "contact")
		{
			// get the contact type
			const char* sztype = tag.AttributeValue("type");
			if      (strcmp(sztype, "sliding_with_gaps"     ) == 0) ParseSlidingInterface     (tag);
			else if (strcmp(sztype, "facet-to-facet sliding") == 0) ParseFacetSlidingInterface(tag);
			else if (strcmp(sztype, "sliding2"              ) == 0) ParseSlidingInterface2    (tag);
			else if (strcmp(sztype, "sliding3"              ) == 0) ParseSlidingInterface3    (tag);
			else if (strcmp(sztype, "tied"                  ) == 0) ParseTiedInterface        (tag);
			else if (strcmp(sztype, "periodic boundary"     ) == 0) ParsePeriodicBoundary     (tag);
			else if (strcmp(sztype, "surface constraint"    ) == 0) ParseSurfaceConstraint    (tag);
			else if (strcmp(sztype, "rigid_wall"            ) == 0) ParseRigidWall            (tag);
			else if (strcmp(sztype, "rigid"                 ) == 0) ParseRigidInterface       (tag);
			else if (strcmp(sztype, "rigid joint"           ) == 0) ParseRigidJoint           (tag);
			else if (strcmp(sztype, "linear constraint"     ) == 0) ParseLinearConstraint     (tag);
			else throw XMLReader::InvalidAttributeValue(tag, "type", sztype);
		}
		else throw XMLReader::InvalidTag(tag);

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- S L I D I N G   W I T H   G A P S ---
void FEBioContactSection::ParseSlidingInterface(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FESlidingInterface* ps = new FESlidingInterface(&fem);
	fem.AddContactInterface(ps);

	FEParameterList& pl = ps->GetParameterList();

	++tag;
	do
	{
		// read parameters
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			if (tag == "surface")
			{
				const char* sztype = tag.AttributeValue("type");
				int ntype;
				if (strcmp(sztype, "master") == 0) ntype = 1;
				else if (strcmp(sztype, "slave") == 0) ntype = 2;

				FESlidingSurface& s = (ntype == 1? ps->m_ms : ps->m_ss);
				m.AddSurface(&s);

				int nfmt = 0;
				const char* szfmt = tag.AttributeValue("format", true);
				if (szfmt)
				{
					if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
					else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
				}

				// read the surface section
				ParseSurfaceSection(tag, s, nfmt);
			}
			else throw XMLReader::InvalidTag(tag);
		}
		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- F A C E T   T O   F A C E T   S L I D I N G ---
void FEBioContactSection::ParseFacetSlidingInterface(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FEFacet2FacetSliding* ps = new FEFacet2FacetSliding(&fem);
	fem.AddContactInterface(ps);

	FEParameterList& pl = ps->GetParameterList();

	++tag;
	do
	{
		// read parameters
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			if (tag == "surface")
			{
				const char* sztype = tag.AttributeValue("type");
				int ntype;
				if (strcmp(sztype, "master") == 0) ntype = 1;
				else if (strcmp(sztype, "slave") == 0) ntype = 2;

				FEFacetSlidingSurface& s = (ntype == 1? ps->m_ms : ps->m_ss);
				m.AddSurface(&s);

				int nfmt = 0;
				const char* szfmt = tag.AttributeValue("format", true);
				if (szfmt)
				{
					if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
					else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
				}

				// read the surface section
				ParseSurfaceSection(tag, s, nfmt);

				// currently the element types are automatically set to FE_NIQUAD or FE_NITRI
				// so we have to modify those elements to FE_QUAD and FE_TRI
				// TODO: we need a better way of doing this!
				for (int i=0; i<s.Elements(); ++i)
				{
					FESurfaceElement& e = s.Element(i);
					if (e.Nodes() == 4) e.SetType(FE_QUAD); 
					else e.SetType(FE_TRI);
				}
			}
			else throw XMLReader::InvalidTag(tag);
		}

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- S L I D I N G   I N T E R F A C E   2 ---
void FEBioContactSection::ParseSlidingInterface2(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FESlidingInterface2* ps = new FESlidingInterface2(&fem);
	fem.AddContactInterface(ps);

	FEParameterList& pl = ps->GetParameterList();

	++tag;
	do
	{
		// read parameters
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			if (tag == "surface")
			{
				const char* sztype = tag.AttributeValue("type");
				int ntype;
				if (strcmp(sztype, "master") == 0) ntype = 1;
				else if (strcmp(sztype, "slave") == 0) ntype = 2;

				FESlidingSurface2& s = (ntype == 1? ps->m_ms : ps->m_ss);
				m.AddSurface(&s);

				int nfmt = 0;
				const char* szfmt = tag.AttributeValue("format", true);
				if (szfmt)
				{
					if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
					else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
				}

				// read the surface section
				ParseSurfaceSection(tag, s, nfmt);

				// currently the element types are automatically set to FE_NIQUAD or FE_NITRI
				// For this type of contact we want gaussian quadrature,
				// so we have to modify those elements to FE_QUAD and FE_TRI
				// TODO: we need a better way of doing this!
				for (int i=0; i<s.Elements(); ++i)
				{
					FESurfaceElement& e = s.Element(i);
					if (e.Nodes() == 4) e.SetType(FE_QUAD); 
					else e.SetType(FE_TRI);
				}
			}
			else throw XMLReader::InvalidTag(tag);
		}

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- S L I D I N G   I N T E R F A C E   3 ---
void FEBioContactSection::ParseSlidingInterface3(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FESlidingInterface3* ps = new FESlidingInterface3(&fem);
	fem.AddContactInterface(ps);

	FEParameterList& pl = ps->GetParameterList();
	
	++tag;
	do
	{
		// read parameters
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			if (tag == "surface")
			{
				const char* sztype = tag.AttributeValue("type");
				int ntype;
				if (strcmp(sztype, "master") == 0) ntype = 1;
				else if (strcmp(sztype, "slave") == 0) ntype = 2;
				
				FESlidingSurface3& s = (ntype == 1? ps->m_ms : ps->m_ss);
				m.AddSurface(&s);
				
				int nfmt = 0;
				const char* szfmt = tag.AttributeValue("format", true);
				if (szfmt)
				{
					if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
					else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
				}
				
				// read the surface section
				ParseSurfaceSection(tag, s, nfmt);
				
				// currently the element types are automatically set to FE_NIQUAD or FE_NITRI
				// For this type of contact we want gaussian quadrature,
				// so we have to modify those elements to FE_QUAD and FE_TRI
				// TODO: we need a better way of doing this!
				for (int i=0; i<s.Elements(); ++i)
				{
					FESurfaceElement& e = s.Element(i);
					if (e.Nodes() == 4) e.SetType(FE_QUAD); 
					else e.SetType(FE_TRI);
				}
			}
			else throw XMLReader::InvalidTag(tag);
		}
		
		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- T I E D   C O N T A C T  ---
void FEBioContactSection::ParseTiedInterface(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FETiedInterface* ps = new FETiedInterface(&fem);
	fem.AddContactInterface(ps);

	FEParameterList& pl = ps->GetParameterList();

	++tag;
	do
	{
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			if (tag == "surface")
			{
				const char* sztype = tag.AttributeValue("type");
				int ntype;
				if (strcmp(sztype, "master") == 0) ntype = 1;
				else if (strcmp(sztype, "slave") == 0) ntype = 2;

				FETiedContactSurface& s = (ntype == 1? ps->ms : ps->ss);
				m.AddSurface(&s);

				int nfmt = 0;
				const char* szfmt = tag.AttributeValue("format", true);
				if (szfmt)
				{
					if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
					else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
				}

				// read the surface section
				ParseSurfaceSection(tag, s, nfmt);
			}
			else throw XMLReader::InvalidTag(tag);
		}

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- P E R I O D I C   B O U N D A R Y  ---
void FEBioContactSection::ParsePeriodicBoundary(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FEPeriodicBoundary* ps = new FEPeriodicBoundary(&fem);
	fem.AddContactInterface(ps);

	FEParameterList& pl = ps->GetParameterList();

	++tag;
	do
	{
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			if (tag == "surface")
			{
				const char* sztype = tag.AttributeValue("type");
				int ntype;
				if (strcmp(sztype, "master") == 0) ntype = 1;
				else if (strcmp(sztype, "slave") == 0) ntype = 2;

				FEPeriodicSurface& s = (ntype == 1? ps->m_ms : ps->m_ss);
				m.AddSurface(&s);

				int nfmt = 0;
				const char* szfmt = tag.AttributeValue("format", true);
				if (szfmt)
				{
					if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
					else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
				}

				// read the surface section
				ParseSurfaceSection(tag, s, nfmt);
			}
			else throw XMLReader::InvalidTag(tag);
		}

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- S U R F A C E   C O N S T R A I N T ---
void FEBioContactSection::ParseSurfaceConstraint(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FESurfaceConstraint* ps = new FESurfaceConstraint(&fem);
	fem.AddContactInterface(ps);

	FEParameterList& pl = ps->GetParameterList();

	++tag;
	do
	{
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			if (tag == "surface")
			{
				const char* sztype = tag.AttributeValue("type");
				int ntype;
				if (strcmp(sztype, "master") == 0) ntype = 1;
				else if (strcmp(sztype, "slave") == 0) ntype = 2;

				FESurfaceConstraintSurface& s = (ntype == 1? ps->m_ms : ps->m_ss);
				m.AddSurface(&s);

				int nfmt = 0;
				const char* szfmt = tag.AttributeValue("format", true);
				if (szfmt)
				{
					if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
					else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
				}

				// read the surface section
				ParseSurfaceSection(tag, s, nfmt);
			}
			else throw XMLReader::InvalidTag(tag);
		}

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- R I G I D   W A L L   I N T E R F A C E ---
void FEBioContactSection::ParseRigidWall(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FERigidWallInterface* ps = new FERigidWallInterface(&fem);
	fem.AddContactInterface(ps);

	FEParameterList& pl = ps->GetParameterList();

	++tag;
	do
	{
		if (m_pim->ReadParameter(tag, pl) == false)
		{
			if (tag == "plane")
			{
				ps->SetMasterSurface(new FEPlane(&fem));
				FEPlane& pl = dynamic_cast<FEPlane&>(*ps->m_mp);
				const char* sz = tag.AttributeValue("lc", true);
				if (sz)	pl.m_nplc = atoi(sz);

				double* a = pl.GetEquation();
				tag.value(a, 4);
			}
			else if (tag == "sphere")
			{
				ps->SetMasterSurface(new FERigidSphere(&fem));
				FERigidSphere& s = dynamic_cast<FERigidSphere&>(*ps->m_mp);
				++tag;
				do
				{
					if      (tag == "center") tag.value(s.m_rc);
					else if (tag == "radius") tag.value(s.m_R);
					else if (tag == "xtrans")
					{
						const char* szlc = tag.AttributeValue("lc");
						s.m_nplc[0] = atoi(szlc);
					}
					else if (tag == "ytrans")
					{
						const char* szlc = tag.AttributeValue("lc");
						s.m_nplc[1] = atoi(szlc);
					}
					else if (tag == "ztrans")
					{
						const char* szlc = tag.AttributeValue("lc");
						s.m_nplc[2] = atoi(szlc);
					}
					else throw XMLReader::InvalidTag(tag);
					++tag;
				}
				while (!tag.isend());
			}
			else if (tag == "surface")
			{
				FERigidWallSurface& s = ps->m_ss;

				int nfmt = 0;
				const char* szfmt = tag.AttributeValue("format", true);
				if (szfmt)
				{
					if (strcmp(szfmt, "face nodes") == 0) nfmt = 0;
					else if (strcmp(szfmt, "element face") == 0) nfmt = 1;
				}

				// read the surface section
				ParseSurfaceSection(tag, s, nfmt);
			}
			else throw XMLReader::InvalidTag(tag);
		}
		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
// --- R I G I D   B O D Y   I N T E R F A C E ---
void FEBioContactSection::ParseRigidInterface(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	// count how many rigid nodes there are
	int nrn= 0;
	XMLTag t(tag); ++t;
	while (!t.isend()) { nrn++; ++t; }

	++tag;
	int id, rb;
	for (int i=0; i<nrn; ++i)
	{
		id = atoi(tag.AttributeValue("id"))-1;
		rb = atoi(tag.AttributeValue("rb"))-1;

		FERigidNode* prn = new FERigidNode;

		prn->nid = id;
		prn->rid = rb;
		fem.m_RN.push_back(prn);

		if (m_pim->m_nsteps > 0)
		{
			GetStep()->AddBoundaryCondition(prn);
			prn->Deactivate();
		}

		++tag;
	}
}

//-----------------------------------------------------------------------------
// --- R I G I D   J O I N T   I N T E R F A C E ---
void FEBioContactSection::ParseRigidJoint(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	FERigidJoint* prj = new FERigidJoint(&fem);
	FEParameterList& pl = prj->GetParameterList();
	++tag;
	do
	{
		if (m_pim->ReadParameter(tag, pl) == false) throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());
	prj->m_nRBa--;
	prj->m_nRBb--;
	fem.m_RJ.push_back(prj);
}

//-----------------------------------------------------------------------------
// --- L I N E A R   C O N S T R A I N T ---
void FEBioContactSection::ParseLinearConstraint(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;

	// make sure there is a constraint defined
	if (tag.isleaf()) return;

	// create a new linear constraint manager
	FELinearConstraintSet* pLCS = new FELinearConstraintSet(&fem);
	fem.m_LCSet.push_back(pLCS);

	// read the linear constraints
	++tag;
	do
	{
		if (tag == "linear_constraint")
		{
			FEAugLagLinearConstraint* pLC = new FEAugLagLinearConstraint;

			FEAugLagLinearConstraint::DOF dof;
			++tag;
			do
			{
				if (tag == "node")
				{
					tag.value(dof.val);
					int node;
					tag.AttributeValue("id", node);
					dof.node = node - 1;

					const char* szbc = tag.AttributeValue("bc");
					if      (strcmp(szbc, "x") == 0) dof.bc = 0;
					else if (strcmp(szbc, "y") == 0) dof.bc = 1;
					else if (strcmp(szbc, "z") == 0) dof.bc = 2;
					else throw XMLReader::InvalidAttributeValue(tag, "bc", szbc);

					pLC->m_dof.push_back(dof);
				}
				else throw XMLReader::InvalidTag(tag);
				++tag;
			}
			while (!tag.isend());

			// add the linear constraint to the system
			pLCS->add(pLC);
		}
		else if (tag == "tol"    ) tag.value(pLCS->m_tol);
		else if (tag == "penalty") tag.value(pLCS->m_eps);
		else if (tag == "maxaug") tag.value(pLCS->m_naugmax);
		else throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());
}

//---------------------------------------------------------------------------------
// parse a surface section for contact definitions
//
bool FEBioContactSection::ParseSurfaceSection(XMLTag &tag, FESurface& s, int nfmt)
{
	FEM& fem = *GetFEM();
	FEMesh& m = fem.m_mesh;
	int NN = m.Nodes();

	// count nr of faces
	int faces = 0, N, nf[4];
	XMLTag t(tag); ++t;
	while (!t.isend()) { faces++; ++t; }

	// allocate storage for faces
	s.create(faces);

	// read faces
	++tag;
	for (int i=0; i<faces; ++i)
	{
		FESurfaceElement& el = s.Element(i);

		if (tag == "quad4") el.SetType(FE_NIQUAD);
		else if (tag == "tri3") el.SetType(FE_NITRI);
		else throw XMLReader::InvalidTag(tag);

		N = el.Nodes();

		if (nfmt == 0)
		{
			tag.value(nf, N);
			for (int j=0; j<N; ++j) 
			{
				int nid = nf[j]-1;
				if ((nid<0)||(nid>= NN)) throw XMLReader::InvalidValue(tag);
				el.m_node[j] = nid;
			}
		}
		else if (nfmt == 1)
		{
			tag.value(nf, 2);
			FEElement* pe = m.FindElementFromID(nf[0]);
			if (pe)
			{
				int ne[4];
				int nn = m.GetFace(*pe, nf[1]-1, ne);
				if (nn != N) throw XMLReader::InvalidValue(tag);
				for (int j=0; j<N; ++j) el.m_node[j] = ne[j];
				el.m_nelem = nf[0];
			}
			else throw XMLReader::InvalidValue(tag);
		}

		++tag;
	}
	return true;
}

//=============================================================================
//
//                     I N I T I A L   S E C T I O N
//
//=============================================================================
//! Read the Initial from the FEBio input file
//!
void FEBioInitialSection::Parse(XMLTag& tag)
{
	if (tag.isleaf()) return;

	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;

	// make sure we've read the nodes section
	if (mesh.Nodes() == 0) throw XMLReader::InvalidTag(tag);

	for (int i=0; i<mesh.Nodes(); ++i) mesh.Node(i).m_v0 = vec3d(0,0,0);

	// read nodal data
	++tag;
	do
	{
		if (tag == "velocity")
		{
			++tag;
			do
			{
				if (tag == "node")
				{
					int nid = atoi(tag.AttributeValue("id"))-1;
					vec3d v;
					tag.value(v);
					mesh.Node(nid).m_v0 += v;
				}
				else throw XMLReader::InvalidTag(tag);
				++tag;
			}
			while (!tag.isend());
		}
		else if (tag == "fluid_pressure")
		{
			++tag;
			do
			{
				if (tag == "node")
				{
					int nid = atoi(tag.AttributeValue("id"))-1;
					double p;
					tag.value(p);
					mesh.Node(nid).m_p0 += p;
				}
				else throw XMLReader::InvalidTag(tag);
				++tag;
			}
			while (!tag.isend());
		}
		else if (tag == "concentration")
		{
			int isol = 0;
			const char* sz = tag.AttributeValue("sol", true);
			if (sz) isol = atoi(sz) - 1;
			if ((isol < 0) || (isol >= MAX_CDOFS))
				throw XMLReader::InvalidAttributeValue(tag, "sol", sz);
			++tag;
			do
			{
				if (tag == "node")
				{
					int nid = atoi(tag.AttributeValue("id"))-1;
					double c;
					tag.value(c);
					mesh.Node(nid).m_c0[isol] += c;
				}
				else throw XMLReader::InvalidTag(tag);
				++tag;
			}
			while (!tag.isend());
		}
		else throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());
}

//=============================================================================
//
//                       G L O B A L S   S E C T I O N
//
//=============================================================================
//
//!  This function reads the global variables from the xml file
//!
void FEBioGlobalsSection::Parse(XMLTag& tag)
{
	FEM& fem = *GetFEM();

	++tag;
	do
	{
		if (tag == "body_force")
		{
			const char* szt = tag.AttributeValue("type", true);
			if (szt == 0) szt = "const";

			if (strcmp(szt, "point") == 0)
			{
				FEPointBodyForce* pf = new FEPointBodyForce(&fem);
				FEParameterList& pl = pf->GetParameterList();
				++tag;
				do
				{
					if (tag == "a")
					{
						const char* szlc = tag.AttributeValue("lc");
//						pf->lc[0] = pf->lc[1] = pf->lc[2] = atoi(szlc);
						tag.value(pf->m_a);
					}
					else if (tag == "node")
					{
						tag.value(pf->m_inode); 
						pf->m_inode -= 1;
					}
					else if (m_pim->ReadParameter(tag, pl) == false) throw XMLReader::InvalidTag(tag);
					++tag;
				}
				while (!tag.isend());

				fem.AddBodyForce(pf);
			}
			else
			{
				// see if the kernel knows this force
				FEBioKernel& febio = FEBioKernel::GetInstance();
				FEBodyForce* pf = febio.Create<FEBodyForce>(szt, &fem);
				if (pf)
				{
					if (!tag.isleaf())
					{
						FEParameterList& pl = pf->GetParameterList();
						++tag;
						do
						{
							if (m_pim->ReadParameter(tag, pl) == false) throw XMLReader::InvalidTag(tag);
							++tag;
						}
						while (!tag.isend());
					}

					fem.AddBodyForce(pf);
				}
				else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
			}
		}
		else if (tag == "Constants")
		{
			++tag;
			string s;
			double v;
			do
			{
				s = string(tag.Name());
				tag.value(v);
				FEM::SetGlobalConstant(s, v);
				++tag;
			}
			while (!tag.isend());
		}

		++tag;
	}
	while (!tag.isend());
}

//=============================================================================
//
//                     L O A D D A T A   S E C T I O N
//
//=============================================================================
//
//!  This function reads the load data section from the xml file
//!
void FEBioLoadSection::Parse(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pstep = GetStep();
	int nmplc = pstep->m_nmplc;

	++tag;
	do
	{
		if (tag == "loadcurve")
		{
			// load curve ID
			int nid;
			tag.AttributeValue("id", nid);

			// default type and extend mode
			FELoadCurve::INTFUNC ntype = FELoadCurve::LINEAR;
			FELoadCurve::EXTMODE nextm = FELoadCurve::CONSTANT;

			// For backward compatibility we must set the default to STEP for the mustpoint loadcurve
			// Note that this assumes that the step has already been read in.
			if (nid == nmplc) ntype = FELoadCurve::STEP;

			// get the (optional) type
			const char* szt = tag.AttributeValue("type", true);
			if (szt)
			{
				if      (strcmp(szt, "step"  ) == 0) ntype = FELoadCurve::STEP;
				else if (strcmp(szt, "linear") == 0) ntype = FELoadCurve::LINEAR;
				else if (strcmp(szt, "smooth") == 0) ntype = FELoadCurve::SMOOTH;
				else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
			}

			// get the optional extend mode
			const char* szm = tag.AttributeValue("extend", true);
			if (szm)
			{
				if      (strcmp(szm, "constant"     ) == 0) nextm = FELoadCurve::CONSTANT;
				else if (strcmp(szm, "extrapolate"  ) == 0) nextm = FELoadCurve::EXTRAPOLATE;
				else if (strcmp(szm, "repeat"       ) == 0) nextm = FELoadCurve::REPEAT;
				else if (strcmp(szm, "repeat offset") == 0) nextm = FELoadCurve::REPEAT_OFFSET;
				else throw XMLReader::InvalidAttributeValue(tag, "extend", szt);
			}

			// count how many points we have
			XMLTag t(tag); ++t;
			int nlp = 0;
			while (!t.isend()) { ++nlp; ++t; }

			// create the loadcurve
			FELoadCurve* plc = new FELoadCurve;
			plc->Create(nlp);
			plc->SetInterpolation(ntype);
			plc->SetExtendMode(nextm);

			// read the points
			double d[2];
			++tag;
			for (int i=0; i<nlp; ++i)
			{
				tag.value(d, 2);
				plc->LoadPoint(i).time  = d[0];
				plc->LoadPoint(i).value = d[1];

				++tag;
			}

			// add the loadcurve to FEM
			fem.AddLoadCurve(plc);
		}
		else throw XMLReader::InvalidTag(tag);

		++tag;
	}
	while (!tag.isend());
}

//=============================================================================
//
//                        O U T P U T   S E C T I O N
//
//=============================================================================
void FEBioOutputSection::Parse(XMLTag& tag)
{
	++tag;
	do
	{
		if (tag == "logfile") ParseLogfile(tag);
		else if (tag == "plotfile") ParsePlotfile(tag);
		else throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
void FEBioOutputSection::ParseLogfile(XMLTag &tag)
{
	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;

	const char* sz;

	// see if the log file has any attributes
	const char* szlog = tag.AttributeValue("file", true);
	if (szlog) fem.SetLogFilename(szlog);

	if (tag.isleaf()) return;
	++tag;
	do
	{
		if (tag == "node_data")
		{
			sz = tag.AttributeValue("file", true);

			NodeDataRecord* prec = new NodeDataRecord(&fem, sz);
			const char* szdata = tag.AttributeValue("data");
			prec->Parse(szdata);

			const char* szname = tag.AttributeValue("name", true);
			if (szname != 0) prec->SetName(szname); else prec->SetName(szdata);

			sz = tag.AttributeValue("delim", true);
			if (sz != 0) prec->SetDelim(sz);

			sz = tag.AttributeValue("comments", true);
			if (sz != 0)
			{
				if      (strcmp(sz, "on") == 0) prec->SetComments(true);
				else if (strcmp(sz, "off") == 0) prec->SetComments(false); 
			}

			if (tag.isleaf()) prec->DataRecord::SetItemList(tag.szvalue());
			else
			{
				++tag;
				if (tag == "node_set")
				{
					FENodeSet* pns = 0;
					const char* szid = tag.AttributeValue("id", true);
					if (szid == 0)
					{
						const char* szname = tag.AttributeValue("name");
						pns = mesh.FindNodeSet(szname);
					}
					else pns = mesh.FindNodeSet(atoi(szid));

					if (pns == 0) throw XMLReader::InvalidAttributeValue(tag, "id", szid);

					prec->SetItemList(pns);
				}
				else throw XMLReader::InvalidTag(tag);
				++tag;
				assert(tag.isend());
			}

			fem.m_Data.AddRecord(prec);
		}
		else if (tag == "element_data")
		{
			sz = tag.AttributeValue("file", true);

			ElementDataRecord* prec = new ElementDataRecord(&fem, sz);
			const char* szdata = tag.AttributeValue("data");
			prec->Parse(szdata);

			const char* szname = tag.AttributeValue("name", true);
			if (szname != 0) prec->SetName(szname); else prec->SetName(szdata);

			sz = tag.AttributeValue("delim", true);
			if (sz != 0) prec->SetDelim(sz);

			sz = tag.AttributeValue("comments", true);
			if (sz != 0)
			{
				if      (strcmp(sz, "on") == 0) prec->SetComments(true);
				else if (strcmp(sz, "off") == 0) prec->SetComments(false); 
			}

			prec->SetItemList(tag.szvalue());

			fem.m_Data.AddRecord(prec);
		}
		else if (tag == "rigid_body_data")
		{
			sz = tag.AttributeValue("file", true);

			RigidBodyDataRecord* prec = new RigidBodyDataRecord(&fem, sz);
			const char* szdata = tag.AttributeValue("data");
			prec->Parse(szdata);

			const char* szname = tag.AttributeValue("name", true);
			if (szname != 0) prec->SetName(szname); else prec->SetName(szdata);

			sz = tag.AttributeValue("delim", true);
			if (sz != 0) prec->SetDelim(sz);

			sz = tag.AttributeValue("comments", true);
			if (sz != 0)
			{
				if      (strcmp(sz, "on") == 0) prec->SetComments(true);
				else if (strcmp(sz, "off") == 0) prec->SetComments(false); 
			}

			prec->SetItemList(tag.szvalue());

			fem.m_Data.AddRecord(prec);
		}
		else if (tag == "echo") tag.value(fem.m_becho);
		else throw XMLReader::InvalidTag(tag);

		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
void FEBioOutputSection::ParsePlotfile(XMLTag &tag)
{
	FEM& fem = *GetFEM();
	FEMesh& mesh = fem.m_mesh;

	const char* sz = tag.AttributeValue("type", true);
	if (sz)
	{
		if (strcmp(sz, "febio") == 0) fem.m_plot = new FEBioPlotFile;
		else if (strcmp(sz, "lsdyna") == 0) fem.m_plot = new LSDYNAPlotFile;
		else throw XMLReader::InvalidAttributeValue(tag, "type", sz);
	}
	else fem.m_plot = new LSDYNAPlotFile;

	const char* szplt = tag.AttributeValue("file", true);
	if (szplt) fem.SetPlotFilename(szplt);

	if (dynamic_cast<LSDYNAPlotFile*>(fem.m_plot) && !tag.isleaf())
	{
		LSDYNAPlotFile& plt = *dynamic_cast<LSDYNAPlotFile*>(fem.m_plot);

		++tag;
		do
		{
			if (tag == "shell_strain") tag.value(plt.m_bsstrn);
			else if (tag == "map")
			{
				const char* szfield = tag.AttributeValue("field");
				const char* szval = tag.szvalue();
				if (strcmp(szfield, "displacement") == 0)
				{
					if (strcmp(szval, "DISPLACEMENT") == 0) plt.m_nfield[0] = PLOT_DISPLACEMENT;
					else throw XMLReader::InvalidValue(tag);
				}
				else if (strcmp(szfield, "velocity") == 0)
				{
					if (strcmp(szval, "NONE") == 0) plt.m_nfield[1] = PLOT_NONE;
					else if (strcmp(szval, "VELOCITY") == 0) plt.m_nfield[1] = PLOT_VELOCITY;
					else if (strcmp(szval, "FLUID_FLUX") == 0) plt.m_nfield[1] = PLOT_FLUID_FLUX;
					else if (strcmp(szval, "CONTACT_TRACTION") == 0) plt.m_nfield[1] = PLOT_CONTACT_TRACTION;
					else if (strcmp(szval, "REACTION_FORCE") == 0) plt.m_nfield[1] = PLOT_REACTION_FORCE;
					else if (strcmp(szval, "MATERIAL_FIBER") == 0) plt.m_nfield[1] = PLOT_MATERIAL_FIBER;
					else throw XMLReader::InvalidValue(tag);
				}
				else if (strcmp(szfield, "acceleration") == 0)
				{
					if (strcmp(szval, "NONE") == 0) plt.m_nfield[2] = PLOT_NONE;
					else if (strcmp(szval, "ACCELERATION") == 0) plt.m_nfield[2] = PLOT_ACCELERATION;
					else if (strcmp(szval, "FLUID_FLUX") == 0) plt.m_nfield[2] = PLOT_FLUID_FLUX;
					else if (strcmp(szval, "CONTACT_TRACTION") == 0) plt.m_nfield[2] = PLOT_CONTACT_TRACTION;
					else if (strcmp(szval, "REACTION_FORCE") == 0) plt.m_nfield[2] = PLOT_REACTION_FORCE;
					else if (strcmp(szval, "MATERIAL_FIBER") == 0) plt.m_nfield[2] = PLOT_MATERIAL_FIBER;
					else throw XMLReader::InvalidValue(tag);
				}
				else if (strcmp(szfield, "temperature") == 0)
				{
					if (strcmp(szval, "NONE") == 0) plt.m_nfield[3] = PLOT_NONE;
					else if (strcmp(szval, "FLUID_PRESSURE") == 0) plt.m_nfield[3] = PLOT_FLUID_PRESSURE;
					else if (strcmp(szval, "CONTACT_PRESSURE") == 0) plt.m_nfield[3] = PLOT_CONTACT_PRESSURE;
					else if (strcmp(szval, "CONTACT_GAP") == 0) plt.m_nfield[3] = PLOT_CONTACT_GAP;
					else throw XMLReader::InvalidValue(tag);
				}
				else if (strcmp(szfield, "plastic strain") == 0)
				{
					if      (strcmp(szval, "PLASTIC_STRAIN"  ) == 0) plt.m_nfield[4] = PLOT_PLASTIC_STRAIN;
					else if (strcmp(szval, "FIBER_STRAIN"    ) == 0) plt.m_nfield[4] = PLOT_FIBER_STRAIN;
					else if (strcmp(szval, "DEV_FIBER_STRAIN") == 0) plt.m_nfield[4] = PLOT_DEV_FIBER_STRAIN;
					else throw XMLReader::InvalidValue(tag);
				}
				else throw XMLReader::InvalidAttributeValue(tag, "field", szfield);
			}
			else throw XMLReader::InvalidTag(tag);
			++tag;
		}
		while (!tag.isend());
	}
	else if (dynamic_cast<FEBioPlotFile*>(fem.m_plot))
	{
		// change the extension of the plot file to .xplt
		fem.SetPlotFileNameExtension(".xplt");

		if (!tag.isleaf())
		{
			FEBioPlotFile& plt = *dynamic_cast<FEBioPlotFile*>(fem.m_plot);
			++tag;
			do
			{
				if (tag == "var")
				{
					const char* szt = tag.AttributeValue("type");
					if (plt.AddVariable(szt) == false) throw XMLReader::InvalidAttributeValue(tag, "type", szt);
				}
				++tag;
			}
			while (!tag.isend());
		}
	}
}

//=============================================================================
//
//                  C O N S T R A I N T S   S E C T I O N
//
//=============================================================================

void FEBioConstraintsSection::Parse(XMLTag &tag)
{
	// This section is only allowed in the new format
	if (m_pim->Version() < 0x0101) throw XMLReader::InvalidTag(tag);

	// make sure there is something to read
	if (tag.isleaf()) return;

	++tag;
	do
	{
		if (tag == "rigid_body") ParseRigidConstraint(tag);
		else if (tag == "point") ParsePointConstraint(tag);
		else throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
void FEBioConstraintsSection::ParseRigidConstraint(XMLTag& tag)
{
	FEM& fem = *GetFEM();
	FEAnalysisStep* pStep = GetStep();

	const char* szm = tag.AttributeValue("mat");
	assert(szm);

	int nmat = atoi(szm);
	if ((nmat <= 0) || (nmat > fem.Materials())) throw XMLReader::InvalidAttributeValue(tag, "mat", szm);

	FERigidMaterial* pm = dynamic_cast<FERigidMaterial*>(fem.GetMaterial(nmat-1));
	if (pm == 0) throw XMLReader::InvalidAttributeValue(tag, "mat", szm);

	++tag;
	do
	{
		if (strncmp(tag.Name(), "trans_", 6) == 0)
		{
			const char* szt = tag.AttributeValue("type");
			const char* szlc = tag.AttributeValue("lc", true);
			int lc = 1;
			if (szlc) lc = atoi(szlc)+1;

			int bc = -1;
			if      (tag.Name()[6] == 'x') bc = 0;
			else if (tag.Name()[6] == 'y') bc = 1;
			else if (tag.Name()[6] == 'z') bc = 2;
			assert(bc >= 0);
			
			if (strcmp(szt, "prescribed") == 0)
			{
				FERigidBodyDisplacement* pDC = new FERigidBodyDisplacement;
				pDC->id = nmat;
				pDC->bc = bc;
				pDC->lc = lc;
				tag.value(pDC->sf);
				fem.m_RDC.push_back(pDC);
				pm->m_bc[bc] = lc;

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					int n = fem.m_RDC.size()-1;
					FERigidBodyDisplacement* pDC = fem.m_RDC[n];
					pStep->AddBoundaryCondition(pDC);
					pDC->Deactivate();
				}
			}
			else if (strcmp(szt, "force") == 0)
			{
				FERigidBodyForce* pFC = new FERigidBodyForce;
				pFC->id = nmat;
				pFC->bc = bc;
				pFC->lc = lc-1;
				tag.value(pFC->sf);
				fem.m_RFC.push_back(pFC);
				pm->m_bc[bc] = 0;

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					int n = fem.m_RFC.size()-1;
					FERigidBodyForce* pFC = fem.m_RFC[n];
					pStep->AddBoundaryCondition(pFC);
					pFC->Deactivate();
				}
			}
			else if (strcmp(szt, "fixed") == 0) pm->m_bc[bc] = -1;
			else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
		}
		else if (strncmp(tag.Name(), "rot_", 4) == 0)
		{
			const char* szt = tag.AttributeValue("type");
			const char* szlc = tag.AttributeValue("lc", true);
			int lc = 0;
			if (szlc) lc = atoi(szlc)+1;

			int bc = -1;
			if      (tag.Name()[4] == 'x') bc = 3;
			else if (tag.Name()[4] == 'y') bc = 4;
			else if (tag.Name()[4] == 'z') bc = 5;
			assert(bc >= 0);

			if (strcmp(szt, "prescribed") == 0)
			{
				FERigidBodyDisplacement* pDC = new FERigidBodyDisplacement;
				pDC->id = nmat;
				pDC->bc = bc;
				pDC->lc = lc;
				tag.value(pDC->sf);
				fem.m_RDC.push_back(pDC);
				pm->m_bc[bc] = lc;

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					int n = fem.m_RDC.size()-1;
					FERigidBodyDisplacement* pDC = fem.m_RDC[n];
					pStep->AddBoundaryCondition(pDC);
					pDC->Deactivate();
				}
			}
			else if (strcmp(szt, "force") == 0)
			{
				FERigidBodyForce* pFC = new FERigidBodyForce;
				pFC->id = nmat;
				pFC->bc = bc;
				pFC->lc = lc-1;
				tag.value(pFC->sf);
				fem.m_RFC.push_back(pFC);
				pm->m_bc[bc] = 0;

				// add this boundary condition to the current step
				if (m_pim->m_nsteps > 0)
				{
					int n = fem.m_RFC.size()-1;
					FERigidBodyForce* pFC = fem.m_RFC[n];
					pStep->AddBoundaryCondition(pFC);
					pFC->Deactivate();
				}
			}
			else if (strcmp(szt, "fixed") == 0) pm->m_bc[bc] = -1;
			else throw XMLReader::InvalidAttributeValue(tag, "type", szt);
		}
		else throw XMLReader::InvalidTag(tag);
		++tag;
	}
	while (!tag.isend());
}

//-----------------------------------------------------------------------------
void FEBioConstraintsSection::ParsePointConstraint(XMLTag &tag)
{
	FEM& fem = *GetFEM();
	int node = -1;
	double	eps;

	++tag;
	do
	{
		if (tag == "node") 
		{
			tag.value(node);
			if (node <= 0) throw XMLReader::InvalidValue(tag);
		}
		else if (tag == "penalty") tag.value(eps);
		++tag;
	}
	while (!tag.isend());
	if (node == -1) throw XMLReader::Error();

	FEPointConstraint pc(&fem);
	pc.m_eps = eps;
	pc.m_node = node-1;
	fem.m_PC.push_back(pc);
}

//=============================================================================
//
//                         S T E P   S E C T I O N
//
//=============================================================================

void FEBioStepSection::Parse(XMLTag& tag)
{
	// We assume that the FEM object will already have at least one step
	// defined. Therefor the first time we find a "step" section we
	// do not create a new step. If more steps are required
	// we need to create new FEAnalysisStep steps and add them to fem
	if (m_pim->m_nsteps != 0)
	{
		// copy the module ID
		assert(m_pim->m_pStep);
		int nmod = m_pim->m_pStep->m_nModule;
		m_pim->m_pStep = new FEAnalysisStep(*m_pim->m_pfem);
		m_pim->m_pfem->AddStep(m_pim->m_pStep);
		m_pim->m_pStep->m_nModule = nmod;
	}

	// increase the step section counter
	++m_pim->m_nsteps;

	FEBioFileSectionMap Map;
	Map["Module"     ] = new FEBioModuleSection     (m_pim);
	Map["Control"    ] = new FEBioControlSection    (m_pim);
	Map["Constraints"] = new FEBioConstraintsSection(m_pim);
	Map["Boundary"   ] = new FEBioBoundarySection   (m_pim);

	++tag;
	do
	{
		std::map<string, FEBioFileSection*>::iterator is = Map.find(tag.Name());
		if (is != Map.end()) is->second->Parse(tag);
		else throw XMLReader::InvalidTag(tag);

		++tag;
	}
	while (!tag.isend());
}
