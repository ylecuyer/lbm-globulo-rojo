#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <omp.h>
#include "ibm.h"
#include "fluid.h"
#include "mesh.h"
#include "memory.h"

#if defined(_WIN32)
	#include <direct.h>
#else
	#include <sys/stat.h>
	#include <sys/types.h>
#endif


using namespace std;

int main(int argc, char *argv[])
{
#if defined(_WIN32)
	_mkdir("temp");
#else
	mkdir("temp", 0777);

#endif

	float X = 21;
	float Y = 21;
	float Z = 21;

	mesh membrana, referencia;
	fluid fluido(X, Y, Z);

	float dt = 1.0;
	float dx = 1.0;

	int VTK = 50;

	// Parametros adimensionales
	float rho = 1.0;
	float nu = 1./6.;
	float Re = 0.5; 
	float G = 0.5;
	float R = Z/5;
	float gamma_dot = (Re*nu)/(rho*pow(R,2));
	float ks = (gamma_dot*nu*R)/(G);
	float kb = ks*1.0e-6;
	float kp = (gamma_dot)/(G);
	float STEPS = 12.0/kp;
	printf("A completar %f iteraciones\n", STEPS);

	// Membrana
	membrana.setID(1);
	membrana.mesh_refine_tri4();
	membrana.mesh_refine_tri4();
	membrana.mesh_refine_tri4();
	membrana.proyectarEsfera(R);
	membrana.proyectarRBC(R);
	membrana.moverCentro((X-1)/2.0, (Y-1)/2.0, (Z-1)/2.0);
	membrana.iniciarGeometria();

	referencia.setID(1);
	referencia.mesh_refine_tri4();
	referencia.mesh_refine_tri4();
	referencia.mesh_refine_tri4();
	referencia.proyectarEsfera(R);
	referencia.proyectarRBC(R);
	referencia.moverCentro((X-1)/2.0, (Y-1)/2.0, (Z-1)/2.0);
	referencia.iniciarGeometria();

	float *cells_f = fluido.get_cells();
	float *flags_f = fluido.get_flags();
	float *rho_f = fluido.get_rho();
	float *fuerza_f = fluido.get_fuerza();
	float *vel_f = fluido.get_vel();

	float *vertex_m = membrana.get_vertex();
	float *velocidad_m = membrana.get_velocidad();
	float *velocidad2_m = membrana.get_velocidad2();

	float *cells_d = NULL;
	float *flags_d = NULL;
	float *rho_d = NULL;
	float *fuerza_d = NULL;
	float *vel_d = NULL;

	float *vertex_d = NULL;
	float *velocidad_d = NULL;
	float *velocidad2_d = NULL;

	int nNodos = membrana.get_nNodos();

	size_t cells_size = 2*X*Y*Z*19*sizeof(float); alloc_memory_GPU(&cells_d, cells_size); send_data_to_GPU(cells_f, cells_d, cells_size);
	size_t flags_size = X*Y*Z*sizeof(float); alloc_memory_GPU(&flags_d, flags_size); send_data_to_GPU(flags_f, flags_d, flags_size);
	size_t vel_size = X*Y*Z*3*sizeof(float); alloc_memory_GPU(&vel_d, vel_size); send_data_to_GPU(vel_f, vel_d, vel_size);
	size_t rho_size = X*Y*Z*sizeof(float); alloc_memory_GPU(&rho_d, rho_size); send_data_to_GPU(rho_f, rho_d, rho_size);
	size_t fuerza_size = X*Y*Z*3*sizeof(float); alloc_memory_GPU(&fuerza_d, fuerza_size); send_data_to_GPU(fuerza_f, fuerza_d, fuerza_size);

	size_t vertex_size = nNodos*3*sizeof(float); alloc_memory_GPU(&vertex_d, vertex_size); send_data_to_GPU(vertex_m, vertex_d, vertex_size);
	size_t velocidad_size = nNodos*3*sizeof(float); alloc_memory_GPU(&velocidad_d, velocidad_size); send_data_to_GPU(velocidad_m, velocidad_d, velocidad_size);
	size_t velocidad2_size = nNodos*3*sizeof(float); alloc_memory_GPU(&velocidad2_d, velocidad2_size); send_data_to_GPU(velocidad2_m, velocidad2_d, velocidad2_size);

	// Fluido
	//From fluid constructor
	fluido.calcularMacro(cells_d, rho_d, vel_d, fuerza_d);
	fluido.setVelocidad(gamma_dot);



	for(int ts = 0 ; ts < STEPS ; ts++)
	{


		// -----------------------------------------------------------------------//
		// 1. Interpolation
		// -----------------------------------------------------------------------//
		int nNodos = membrana.get_nNodos();

		interpolation(vel_d, vertex_d, velocidad_d, velocidad2_d, nNodos, X, Y, Z);
		// -----------------------------------------------------------------------//
		// 2. Encontrar nuevas posiciones de la membrana
		// -----------------------------------------------------------------------//
		membrana.moverNodos(dt, dx);

		// -----------------------------------------------------------------------//
		// 3. Calcular fuerzas en los nodos de la membrana
		// -----------------------------------------------------------------------//
		membrana.calcularFuerzasHelfrich(kb);
		membrana.calcularFuerzasFEM(referencia, ks);


		//Good up to here

		// -----------------------------------------------------------------------//
		// 4. Propagar la densidad de fuerza hacia el fluido
		// -----------------------------------------------------------------------//
		spread(fluido, membrana, X, Y, Z);

		// -----------------------------------------------------------------------//
		// 5. Solucionar la dinámica del fluido
		// -----------------------------------------------------------------------//
		fluido.collide(cells_d, fuerza_d);
		fluido.stream(cells_d, flags_d);

		// -----------------------------------------------------------------------//
		// 6. Calcular propiedades macro del fluido
		// -----------------------------------------------------------------------//
		fluido.calcularMacro(cells_d, rho_d, vel_d, fuerza_d);

		// -----------------------------------------------------------------------//
		// 7. Calcular propiedades macro de la membrana
		// -----------------------------------------------------------------------//
		membrana.calcularCambioArea(referencia);

		// -----------------------------------------------------------------------//
		// 9. Visualización
		// -----------------------------------------------------------------------//
		if(ts%VTK==0)
		{
			retrieve_data_from_GPU(cells_f, cells_d, cells_size);
			retrieve_data_from_GPU(flags_f, flags_d, flags_size);
			retrieve_data_from_GPU(vel_f, vel_d, vel_size);
			retrieve_data_from_GPU(rho_f, rho_d, rho_size);
			retrieve_data_from_GPU(fuerza_f, fuerza_d, fuerza_size);
			retrieve_data_from_GPU(vertex_m, vertex_d, vertex_size);
			retrieve_data_from_GPU(velocidad_m, velocidad_d, velocidad_size);
			retrieve_data_from_GPU(velocidad2_m, velocidad2_d, velocidad2_size);

			fluido.guardar(ts);
			membrana.guardarVTU(ts);
			printf("%d\n",ts);
		}
	}//Ciclo principal

	free_memory_GPU(cells_d);
	free_memory_GPU(flags_d);
	free_memory_GPU(vel_d);
	free_memory_GPU(fuerza_d);
	free_memory_GPU(vertex_d);
	free_memory_GPU(velocidad_d);
	free_memory_GPU(velocidad2_d);



	return 0;
}
