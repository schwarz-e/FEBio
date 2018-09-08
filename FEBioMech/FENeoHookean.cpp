#include "stdafx.h"
#include "FENeoHookean.h"

//-----------------------------------------------------------------------------
// define the material parameters
BEGIN_PARAMETER_LIST(FENeoHookean, FEElasticMaterial)
	ADD_PARAMETER(m_E, FE_PARAM_DOUBLE_MAPPED, "E");
	ADD_PARAMETER(m_v, FE_PARAM_DOUBLE_MAPPED, "v");
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
mat3ds FENeoHookean::Stress(FEMaterialPoint& mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	double detF = pt.m_J;
	double detFi = 1.0/detF;
	double lndetF = log(detF);

	// get the material parameters
	double E = m_E.eval(mp);
	double v = m_v.eval(mp);

	// calculate left Cauchy-Green tensor
	mat3ds b = pt.LeftCauchyGreen();

	// lame parameters
	double lam = v*E/((1+v)*(1-2*v));
	double mu  = 0.5*E/(1+v);

	// Identity
	mat3dd I(1);

	// calculate stress
	mat3ds s = (b - I)*(mu*detFi) + I*(lam*lndetF*detFi);

	return s;
}

//-----------------------------------------------------------------------------
tens4ds FENeoHookean::Tangent(FEMaterialPoint& mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();

	// deformation gradient
	double detF = pt.m_J;

	// get the material parameters
	double E = m_E.eval(mp);
	double v = m_v.eval(mp);

	// lame parameters
	double lam = v*E/((1+v)*(1-2*v));
	double mu  = 0.5*E/(1+v);

	double lam1 = lam / detF;
	double mu1  = (mu - lam*log(detF)) / detF;
	
	double D[6][6] = {0};
	D[0][0] = lam1+2.*mu1; D[0][1] = lam1       ; D[0][2] = lam1       ;
	D[1][0] = lam1       ; D[1][1] = lam1+2.*mu1; D[1][2] = lam1       ;
	D[2][0] = lam1       ; D[2][1] = lam1       ; D[2][2] = lam1+2.*mu1;
	D[3][3] = mu1;
	D[4][4] = mu1;
	D[5][5] = mu1;

	return tens4ds(D);
}

//-----------------------------------------------------------------------------
double FENeoHookean::StrainEnergyDensity(FEMaterialPoint& mp)
{
	FEElasticMaterialPoint& pt = *mp.ExtractData<FEElasticMaterialPoint>();
	
	double J = pt.m_J;
	double lnJ = log(J);
	
	// get the material parameters
	double E = m_E.eval(mp);
	double v = m_v.eval(mp);

	// calculate left Cauchy-Green tensor
	mat3ds b = pt.LeftCauchyGreen();
	double I1 = b.tr();
	
	// lame parameters
	double lam = v*E/((1+v)*(1-2*v));
	double mu  = 0.5*E/(1+v);
	
	double sed = mu*((I1-3)/2.0 - lnJ)+lam*lnJ*lnJ/2.0;
	
	return sed;
}

//-----------------------------------------------------------------------------
mat3ds FENeoHookean::PK2Stress(FEMaterialPoint& pt, const mat3ds ES)
{
    // Identity
    mat3dd I(1);
    
    // calculate right Cauchy-Green tensor
    mat3ds C = I + ES*2;
    mat3ds Ci = C.inverse();
    
    double detF = sqrt(C.det());
    double lndetF = log(detF);
    
	// get the material parameters
	double E = m_E.eval(pt);
	double v = m_v.eval(pt);
    
    // lame parameters
    double lam = v*E/((1+v)*(1-2*v));
    double mu  = 0.5*E/(1+v);
    
    // calculate stress
    mat3ds S = (I - Ci)*mu + Ci*(lam*lndetF);
    
    return S;
}

//-----------------------------------------------------------------------------
tens4ds FENeoHookean::MaterialTangent(FEMaterialPoint& pt, const mat3ds ES)
{
    // calculate right Cauchy-Green tensor
    mat3ds C = mat3dd(1) + ES*2;
    mat3ds Ci = C.inverse();
    double J = sqrt(C.det());

	// get the material parameters
	double E = m_E.eval(pt);
	double v = m_v.eval(pt);

    // lame parameters
    double lam = v*E/((1+v)*(1-2*v));
    double mu  = 0.5*E/(1+v);
    
    tens4ds c = dyad1s(Ci)*lam + dyad4s(Ci)*(2*(mu-lam*log(J)));
    
    return c;
}
