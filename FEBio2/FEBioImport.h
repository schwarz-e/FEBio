#pragma once

#include "FileImport.h"
#include "FECore/XMLReader.h"
#include "FETransverselyIsotropic.h"
#include "FERigid.h"
#include "FEElasticMixture.h"
#include "FEUncoupledElasticMixture.h"
#include "FEBiphasic.h"
#include <map>
#include <string>
using namespace std;

class FEFEBioImport;

//-----------------------------------------------------------------------------
// Base class for XML sections parsers
class FEBioFileSection
{
public:
	FEBioFileSection(FEFEBioImport* pim) { m_pim = pim; }

	virtual void Parse(XMLTag& tag) = 0;

	FEM* GetFEM();
	FEAnalysis* GetStep();

protected:
	FEFEBioImport*	m_pim;
};

//-----------------------------------------------------------------------------
// class that manages file section parsers
class FEBioFileSectionMap : public map<string, FEBioFileSection*>
{
public:
	~FEBioFileSectionMap();
};

//-----------------------------------------------------------------------------
// Import section
class FEBioImportSection : public FEBioFileSection
{
public:
	FEBioImportSection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);
};

//-----------------------------------------------------------------------------
// Module Section
class FEBioModuleSection : public FEBioFileSection
{
public:
	FEBioModuleSection(FEFEBioImport* pim) : FEBioFileSection(pim) {}
	void Parse(XMLTag& tag);
};

//-----------------------------------------------------------------------------
// Control Section
class FEBioControlSection : public FEBioFileSection
{
public:
	FEBioControlSection(FEFEBioImport* pim) : FEBioFileSection(pim) {}
	void Parse(XMLTag& tag);

protected:
	FESolver* BuildSolver(int nmod, FEM& fem);

	bool ParseCommonParams(XMLTag& tag);
	bool ParseSolidParams (XMLTag& tag);
	bool ParsePoroParams  (XMLTag& tag);
	bool ParseSoluteParams(XMLTag& tag);
};

//-----------------------------------------------------------------------------
// Material Section
class FEBioMaterialSection : public FEBioFileSection
{
public:
	FEBioMaterialSection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);

protected:
	void ParseMaterial					(XMLTag& tag, FEMaterial* pm);
	bool ParseElasticMaterial			(XMLTag& tag, FEElasticMaterial* pm);
	bool ParseTransIsoMaterial			(XMLTag& tag, FETransverselyIsotropic* pm);
	bool ParseRigidMaterial				(XMLTag& tag, FERigidMaterial* pm);
	bool ParseElasticMixture			(XMLTag& tag, FEElasticMixture* pm);
	bool ParseUncoupledElasticMixture	(XMLTag& tag, FEUncoupledElasticMixture* pm);
	bool ParseBiphasicMaterial			(XMLTag& tag, FEBiphasic* pm);
	bool ParseBiphasicSoluteMaterial	(XMLTag& tag, FEBiphasicSolute* pm);

protected:
	int	m_nmat;
};

//-----------------------------------------------------------------------------
// Geometry Section
class FEBioGeometrySection : public FEBioFileSection
{
private:
	enum {
		ET_HEX,
		ET_PENTA,
		ET_TET,
		ET_QUAD,
		ET_TRI,
		ET_TRUSS
	};

	struct FEDOMAIN 
	{
		int		mat;	// material ID
		int		elem;	// element type
		int		nel;	// number of elements
	};
	
public:
	FEBioGeometrySection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);

protected:
	void ParseNodeSection       (XMLTag& tag);
	void ParseElementSection    (XMLTag& tag);
	void ParseElementDataSection(XMLTag& tag);
	void ParseGroupSection      (XMLTag& tag);

	void ReadSolidElement(XMLTag& tag, FESolidElement& el, int ntype, int nid, int gid, int nmat);
	void ReadShellElement(XMLTag& tag, FEShellElement& el, int ntype, int nid, int gid, int nmat);
	void ReadTrussElement(XMLTag& tag, FETrussElement& el, int ntype, int nid, int gid, int nmat);

	int ElementType(XMLTag& tag);
	int DomainType(int etype, FEMaterial* pmat);
	FEDomain* CreateDomain(int ntype, FEMesh* pm, FEMaterial* pmat);
};

//-----------------------------------------------------------------------------
// Boundary Section
class FEBioBoundarySection : public FEBioFileSection
{
public:
	FEBioBoundarySection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);

protected:
	void ParseBCFix               (XMLTag& tag);
	void ParseBCPrescribe         (XMLTag& tag);
	void ParseBCForce             (XMLTag& tag);
	void ParseBCPressure          (XMLTag& tag);
	void ParseBCTraction          (XMLTag& tag);
	void ParseBCPoroNormalTraction(XMLTag& tag);
	void ParseBCFluidFlux         (XMLTag& tag);
	void ParseBCSoluteFlux        (XMLTag &tag);
	void ParseBCHeatFlux          (XMLTag& tag);
	void ParseContactSection      (XMLTag& tag);
	void ParseConstraints         (XMLTag& tag);
	void ParseSpringSection       (XMLTag& tag);
	bool ParseSurfaceSection      (XMLTag& tag, FESurface& s, int nfmt);
};

//-----------------------------------------------------------------------------
// Initial Section
class FEBioInitialSection : public FEBioFileSection
{
public:
	FEBioInitialSection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);
};

//-----------------------------------------------------------------------------
// Globals Section
class FEBioGlobalsSection : public FEBioFileSection
{
public:
	FEBioGlobalsSection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);
};

//-----------------------------------------------------------------------------
// LoadData Section
class FEBioLoadSection : public FEBioFileSection
{
public:
	FEBioLoadSection(FEFEBioImport* pim) : FEBioFileSection(pim) {}
	void Parse(XMLTag& tag);
};

//-----------------------------------------------------------------------------
// Output Section
class FEBioOutputSection : public FEBioFileSection
{
public:
	FEBioOutputSection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);

protected:
	void ParseLogfile (XMLTag& tag);
	void ParsePlotfile(XMLTag& tag);
};

//-----------------------------------------------------------------------------
// Constraints Section
class FEBioConstraintsSection : public FEBioFileSection
{
public:
	FEBioConstraintsSection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);

protected:
	void ParseRigidConstraint(XMLTag& tag);
	void ParsePointConstraint(XMLTag& tag);
};

//-----------------------------------------------------------------------------
// Step Section
class FEBioStepSection : public FEBioFileSection
{
public:
	FEBioStepSection(FEFEBioImport* pim) : FEBioFileSection(pim){}
	void Parse(XMLTag& tag);
};

//=============================================================================
//! Implements a class to import FEBio input files
//!
class FEFEBioImport : public FEFileImport
{
public:
	// Element types
	enum { ET_HEX8, ET_PENTA6, ET_TET4, ET_UT4, ET_TETG1, ET_QUAD4, ET_TRI3, ET_TRUSS2 };

	// element classes
	enum { EC_STRUCT, EC_RIGID, EC_PORO, EC_HEAT };

public:
	class InvalidVersion{};
	class InvalidMaterial
	{ 
	public: 
		InvalidMaterial(int nel) : m_nel(nel){}
		int m_nel; 
	};
	class InvalidDomainType{};
	class FailedCreatingDomain{};
	class InvalidElementType{};
	class FailedLoadingPlugin
	{
	public:
		FailedLoadingPlugin(const char* sz) : m_szfile(sz) {}
		const char* FileName() { return m_szfile.c_str(); }
	public:
		string	m_szfile;
	};

public:
	bool Load(FEM& fem, const char* szfile);

	FEM* GetFEM() { return m_pfem; }
	FEAnalysis*	GetStep() { return m_pStep; }

	int Version() { return m_nversion; }

	bool ReadParameter(XMLTag& tag, FEParameterList& pl);

protected:
	void ParseVersion			(XMLTag& tag);

public:
	FEM*		m_pfem;		//!< pointer to the fem class
	FEAnalysis*	m_pStep;	//!< pointer to current analysis step

protected:
	XMLReader	m_xml;	//!< the actual reader

public:
	int	m_ntet4;	// tetrahedral integration rule
	int	m_nut4;		// integration rule for stabilization of UT4
	int m_nsteps;	// nr of step sections read
	int	m_nmat;		// nr of materials

	bool	m_b3field;	// three-field element flag
	int		m_nhex8;	// hex integration rule

protected:
	int	m_nversion;	// version of file
};
