#include "DiffusionSimulator.h"
#include "pcgsolver.h"
using namespace std;

#define GRID_DIM 40 // columns and rows
#define GRID_SIZE 2 // Absolute distance to which the grid is scaled

#define SPATIAL_DELTA 10
#define WAVE_SPEED 300
#define CULLING_PROJECTION_RADIUS 6
#define WATER_ZERO_HEIGHT -0.5
#define DAMPING 0.999
#define WATER_COLLISION_FACTOR 0.4
#define WATER_COLLISION_DOWNSPEED 0.5
#define BOUNCYNESS 0.7


// --- Rigid body ---

ExternalForce::ExternalForce(Vec3 force, Vec3 position) {
	this->force = force;
	this->position = position;
}

Vec3 ExternalForce::convertToTorque(Vec3 centerOfMass) {
	Vec3 localSpacePos = position - centerOfMass;
	return cross(localSpacePos, force);
}


RigidBody::RigidBody(Vec3 position_x, Quat orientation_r, Vec3 size, float mass_m) {
	this->position_x = position_x;
	this->size = size;
	this->orientation_r = orientation_r;
	this->mass_m = mass_m;

	this->initInverse_I_0();
}

// TODO: Check if Mat3 instead of Mat4 is necessary/possible!
void RigidBody::initInverse_I_0() {
	// Specific to rectangles

	float width = size[0];
	float height = size[1];
	float depth = size[2];

	float fak = mass_m / 12;

	float I_11 = fak * (std::pow(height, 2) + std::pow(depth, 2));
	float I_22 = fak * (std::pow(width, 2) + std::pow(height, 2));
	float I_33 = fak * (std::pow(width, 2) + std::pow(depth, 2));

	double arr[16] = { I_11, 0, 0, 0, 0, I_22, 0, 0, 0, 0, I_33, 0, 0, 0, 0, 1 };

	Inverse_I_0 = Mat4();
	Inverse_I_0.initFromArray(arr);
	Inverse_I_0 = Inverse_I_0.inverse();
}

Mat4 RigidBody::getInverseInertiaTensorRotated() {
	auto rotMat = orientation_r.getRotMat();
	auto rotMat_T = orientation_r.getRotMat();
	rotMat_T.transpose();
	return rotMat * Inverse_I_0 * rotMat_T;
}

Mat4 RigidBody::getObject2WorldMatrix() {
	Mat4 scaleMat = Mat4();
	scaleMat.initScaling(size.x, size.y, size.z);

	Mat4 translatMat = Mat4();
	translatMat.initTranslation(position_x.x, position_x.y, position_x.z);

	return scaleMat * orientation_r.getRotMat() * translatMat; // scaleMat * rotMat * translatMat;
}

// 0 at last position!!
Quat RigidBody::getAngularVelocityQuat()
{
	return Quat(angularVelocity_w.x, angularVelocity_w.y, angularVelocity_w.z, 0);
}

void RigidBody::applyExternalForce(ExternalForce* force)
{
	externalForces.push_back(force);
}

Vec3 RigidBody::sumTotalForce_F()
{
	Vec3 out;
	for each (auto eForce in externalForces) {
		out += eForce->force;
	}

	return out;
}

Vec3 RigidBody::sumTotalTorque_q()
{
	Vec3 out;
	for each (auto eForce in externalForces) {
		out += eForce->convertToTorque(position_x);
	}

	return out;
}

Vec3 RigidBody::localToWoldPosition(Vec3 localPosition)
{
	return position_x + orientation_r.getRotMat().transformVector(localPosition);
}

Vec3 RigidBody::getTotalVelocityAtLocalPositiion(Vec3 localPosition)
{
	return linearVelocity_v + cross(angularVelocity_w, localPosition);
}

void RigidBody::printState()
{
	// linearVelocity_v; angularVelocity_w; angularMomentum_L;
	std::cout << "position x: " << position_x << endl;
	std::cout << "v: " << linearVelocity_v << endl;
	std::cout << "r: " << orientation_r << endl;
	std::cout << "L: " << angularMomentum_L << endl;
	std::cout << "w: " << angularVelocity_w << "; InvI (rot):";
	std::cout << "InvI(rot) :";
	std::cout << getInverseInertiaTensorRotated() << endl;
}

Vec3 DiffusionSimulator::getPositionOfRigidBody(int i) {
	return rigidBodies[i]->position_x;
}

void DiffusionSimulator::applyForceOnBody(int i, Vec3 loc, Vec3 force) {
	rigidBodies[i]->applyExternalForce(new ExternalForce(force, loc));
}

void DiffusionSimulator::initSetup_RB() {
	rigidBodies.clear();
	
	Vec3 size = Vec3(.1, 0.1, 0.1);

	Vec3 position_1 = Vec3(-1, 1, -1);
	Vec3 dir_1 = Vec3(1, -1, 1) * 3;

	Mat4 rot_1 = Mat4();
	rot_1.initRotationXYZ(0, 0, 0);

	Vec3 position_2 = Vec3(1, 1, 1);
	Vec3 dir_2 = Vec3(-1, -1, -1) * 3;

	Mat4 rot_2 = Mat4();
	rot_2.initRotationXYZ(0, 45, 0);


	RigidBody* rect_1 = new RigidBody(position_1, Quat(rot_1), size, 1);
	RigidBody* rect_2 = new RigidBody(position_2, Quat(rot_2), size, 1);


	// Initial velocity
	rect_1->linearVelocity_v = dir_1;
	rect_2->linearVelocity_v = dir_2;


	rigidBodies.push_back(rect_1);
	rigidBodies.push_back(rect_2);
}

Quat normalzeQuat(Quat quaternion) {
	auto norm = quaternion.norm();
	if (norm > 0) {
		quaternion /= norm;
	}
	return quaternion;
}

void DiffusionSimulator::simulateTimestep_RB(float timeStep)
{
	handleCollisions();

	for each (auto body in rigidBodies) {

		/* Linear Part */

		// Integrate positon of x_cm (linear translation)
		body->position_x += timeStep * body->linearVelocity_v;
		// Integrate velocity of x_cm
		Vec3 acceleration = body->sumTotalForce_F() / body->mass_m;
		body->linearVelocity_v += timeStep * acceleration;

		/* Angular Part */

		// Integrate Orientation r
		Quat wr = body->getAngularVelocityQuat() * body->orientation_r;
		body->orientation_r += timeStep / 2 * wr; // TODO: Verify
		body->orientation_r = normalzeQuat(body->orientation_r);

		// Integrate Angular Momentum L
		body->angularMomentum_L += body->sumTotalTorque_q() * timeStep;

		// Update angular velocity using I and L
		body->angularVelocity_w = body->getInverseInertiaTensorRotated().transformVector(body->angularMomentum_L);
	}
}

Real vec_length(Vec3 v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

void DiffusionSimulator::handleCollisions()
{
	// Handle collisions between rigid bodies
	for (int i = 0; i < rigidBodies.size(); i++) {
		for (int j = 0; j < i; j++) {
			handleOneCollision(i, j);
		}
	}

	// Handle collision with water
	for each (auto rb in rigidBodies) {
		if (rb->gridHit) {
			continue;
		}

		for each (auto gp in getProjectedPixels(rb->position_x)) {
			auto info = checkCollisionSAT(rb->getObject2WorldMatrix(), gp->getObject2WorldMatrix());
			if (info.isValid) {
				rb->gridHit = true;

				auto current = T.get(gp->x, gp->y);
				auto v = vec_length(rb->linearVelocity_v);
				auto impulse = rb->mass_m * v;
				T.set(gp->x, gp->y, current + impulse * WATER_COLLISION_FACTOR);
			}
		}

		if (rb->gridHit) {
			// Move rb downwards after collision with water
			rb->linearVelocity_v.y -= vec_length(rb->linearVelocity_v) * WATER_COLLISION_DOWNSPEED;
		}
	}

	std::vector<int> rb_indices_to_delete = std::vector<int>();

	//Handle collision with wall
	for (int i = 0; i < rigidBodies.size(); ++i) {
		auto rb = rigidBodies.at(i);
		// wall at 1 x (rechts)
		if ((rb->position_x.x + rb->size.x * 0.5) > 1) {
			if (rb->linearVelocity_v.x > 0) {
				rb->linearVelocity_v.x = -rb->linearVelocity_v.x;
			}
		}
		// wall at - 1 x (links)
		if ((rb->position_x.x + rb->size.x * 0.5) < - 1) {
			if (rb->linearVelocity_v.x < 0) {
				rb->linearVelocity_v.x = -rb->linearVelocity_v.x;
			}
		}

		// wall at 1 z
		if ((rb->position_x.z + rb->size.z * 0.5) > 1) {
			if (rb->linearVelocity_v.z > 0) {
				rb->linearVelocity_v.z = -rb->linearVelocity_v.z;
			}
		}
		// wall at - 1 z
		if ((rb->position_x.z + rb->size.z * 0.5) < -1) {
			if (rb->linearVelocity_v.z < 0) {
				rb->linearVelocity_v.z = -rb->linearVelocity_v.z;
			}
		}

		// wall at 1.5 y (oben)
		if ((rb->position_x.y + rb->size.y * 0.5) > 1.5) {
			if (rb->linearVelocity_v.y > 0) {
				rb->linearVelocity_v.y = -rb->linearVelocity_v.y;
			}
		}

		if (rb->gridHit && rb->position_x.y + rb->size.y * 0.5 < WATER_ZERO_HEIGHT) {
			rb_indices_to_delete.push_back(i);
		}
	}

	for (int i = rb_indices_to_delete.size() - 1; i >= 0; --i)
	{
		rigidBodies.erase(rigidBodies.begin() + rb_indices_to_delete.at(i));
	}

}


void DiffusionSimulator::handleOneCollision(int indexA, int indexB)
{	
	RigidBody* A = rigidBodies[indexA];
	RigidBody* B = rigidBodies[indexB];

	auto info = checkCollisionSAT(A->getObject2WorldMatrix(), B->getObject2WorldMatrix());
	if (!info.isValid) return;

	//todo is this right is this wrong who am i to disagree
	A->externalForces.clear();
	B->externalForces.clear();


	const Vec3 n = info.normalWorld; // From B to A

	Vec3 x_a = info.collisionPointWorld - A->position_x;
	Vec3 x_b = info.collisionPointWorld - B->position_x;

	Vec3 v_rel = A->getTotalVelocityAtLocalPositiion(x_a) - B->getTotalVelocityAtLocalPositiion(x_b);

	auto v_rel_dot_n = dot(v_rel, n);

	if (v_rel_dot_n > 0) {
		// Bodies are already separating
		return;
	}

	// Further compute formula
	Vec3 intermediate = cross(A->getInverseInertiaTensorRotated().transformVector(cross(x_a, n)), x_a) +
		cross(B->getInverseInertiaTensorRotated().transformVector(cross(x_b, n)), x_b);
	auto J = (-(1 + BOUNCYNESS) * v_rel_dot_n) / ((1 / A->mass_m) + (1 / B->mass_m) + dot(intermediate, n));
	
	// Update

	Vec3 Jn = J * n;

	// Linear
	A->linearVelocity_v += Jn / A->mass_m;
	B->linearVelocity_v -= Jn / B->mass_m;

	// Angular
	A->angularMomentum_L += cross(x_a, Jn);
	B->angularMomentum_L -= cross(x_b, Jn);
}

void DiffusionSimulator::onClick(int x, int y)
{
	m_trackmouse.x = x;
	m_trackmouse.y = y;
	

	if (!chargingForce) {
		chargingForce = true;

		if (duringCreationRigidBody == nullptr) {
			// get coords in worldspace from screen space coord :O
			Mat4 worldViewInv = Mat4(DUC->g_camera.GetWorldMatrix() * DUC->g_camera.GetViewMatrix());
			worldViewInv = worldViewInv.inverse();

			// get actual screen width and height
			auto windowWidth = DXUTGetWindowWidth();
			auto windowHeight = DXUTGetWindowHeight();
			//std::cout << "window: " << windowHeight << "; " << windowWidth << std::endl;

			Vec3 position = Vec3(x, y, 0);
			Vec3 halfScreen = Vec3(windowWidth/2, windowHeight/2, 1);
			//Vec3 halfScreen = Vec3(630, 320, 1);

			Vec3 homoneneousPosition = (position - halfScreen) / halfScreen;

			Vec3 cameraPosition = worldViewInv.transformVector(Vec3(0, 0, 0));
			double cameraDistance = sqrt(cameraPosition.squaredDistanceTo(Vec3(0, 0, 0)));
			//std::cout << "cameraDistance: " << cameraDistance << std::endl;

			// the old magic numbers, but not anymore... :D
			//homoneneousPosition.z = 2;
			//homoneneousPosition.y = -0.8 * homoneneousPosition.y;
			//homoneneousPosition.x = 1.65 * homoneneousPosition.x;
			homoneneousPosition.z = cameraDistance;
			homoneneousPosition.y = -0.4 * homoneneousPosition.z * homoneneousPosition.y;
			homoneneousPosition.x = 0.77 * homoneneousPosition.z * homoneneousPosition.x;
			//std::cout << "position : " << position << "; homoneneous position: " << homoneneousPosition << std::endl;

			Vec3 worldPosition = worldViewInv.transformVector(homoneneousPosition);


			Mat4 rotation = Mat4();

			duringCreationRigidBody = new RigidBody(worldPosition, Quat(rotation), Vec3(0.1, 0.1, 0.1), 0.1f);
			//std::cout << "world position: " << duringCreationRigidBody->position_x << std::endl;
		}
	}
		if (chargingForce && duringCreationRigidBody != nullptr) {
			
			// calculate
			Point2D mouseDiff;
			mouseDiff.x = m_trackmouse.x - m_oldtrackmouse.x;
			mouseDiff.y = m_trackmouse.y - m_oldtrackmouse.y;
			if (mouseDiff.x != 0 || mouseDiff.y != 0)
			{
				Mat4 worldViewInv = Mat4(DUC->g_camera.GetWorldMatrix() * DUC->g_camera.GetViewMatrix());
				worldViewInv = worldViewInv.inverse();
				Vec3 inputView = Vec3((float)mouseDiff.x, (float)-mouseDiff.y, 0);
				Vec3 inputWorld = worldViewInv.transformVectorNormal(inputView);

				Vec3 up = Vec3(0, 1, 0);
				Quat rotationToMoveDirection;
				Vec3 crossV = cross(up, inputWorld);
				rotationToMoveDirection.x = crossV.x;
				rotationToMoveDirection.y = crossV.y;
				rotationToMoveDirection.z = crossV.z;

				rotationToMoveDirection.w = sqrt((dot(inputWorld, inputWorld)) * (dot(up,up))) + dot(inputWorld, up);
				
				duringCreationRigidBody->orientation_r = rotationToMoveDirection.unit();
				//std::cout << duringCreationRigidBody->orientation_r << std::endl;
			}
		}
}

void DiffusionSimulator::onMouse(int x, int y)
{
	if (chargingForce) {
		chargingForce = false;

		// a new rigid bodie is born
		int id = rigidBodies.size();
		rigidBodies.push_back(duringCreationRigidBody);

		// calculate
		Point2D mouseDiff;
		mouseDiff.x = m_trackmouse.x - m_oldtrackmouse.x;
		mouseDiff.y = m_trackmouse.y - m_oldtrackmouse.y;
		if (mouseDiff.x != 0 || mouseDiff.y != 0)
		{
			Mat4 worldViewInv = Mat4(DUC->g_camera.GetWorldMatrix() * DUC->g_camera.GetViewMatrix());
			worldViewInv = worldViewInv.inverse();
			Vec3 inputView = Vec3((float)mouseDiff.x, (float)-mouseDiff.y, 0);
			Vec3 inputWorld = worldViewInv.transformVectorNormal(inputView);
			
			duringCreationRigidBody->linearVelocity_v = inputWorld * -0.05;

		}

		duringCreationRigidBody = nullptr;
	}

	m_oldtrackmouse.x = x;
	m_oldtrackmouse.y = y;
	m_trackmouse.x = x;
	m_trackmouse.y = y;
}

// --- PDE ---

int index(int row, int col, int totalCols) {
	return row * totalCols + col;
}

void GridPixel::update() {
	Real value = grid->get(x, y);

	Real scaleHorizontal = 1.0 / GRID_DIM * GRID_SIZE ;

	Real scaledValue = value / 100;

	this->pos = Vec3(
		x * scaleHorizontal - (GRID_SIZE / 2),
		scaledValue + WATER_ZERO_HEIGHT,
		y * scaleHorizontal - (GRID_SIZE / 2)
	);

	Mat4 posMat = Mat4();
	posMat.initTranslation(pos.x, pos.y, pos.z);

	Mat4 sizeMat = Mat4();
	sizeMat.initScaling(scaleHorizontal, abs(scaledValue) + .01, scaleHorizontal);

	object2WorldMatrix = sizeMat * posMat;

	float white_part = std::min(1.0, abs(scaledValue));
	color = Vec3(white_part, white_part, 1);
}


void GridPixel::draw(DrawingUtilitiesClass* DUC)
{
	DUC->setUpLighting(color, color, 100, color);
	DUC->drawRigidBody(object2WorldMatrix);
}

std::vector<GridPixel*> GridPixel::initPixelsFromGrid(Grid* grid)
{
	std::vector<GridPixel*> pixels;

	auto normInterval = grid->getValueInterval();

	for (int i = 0; i < grid->rows; ++i) {
		for (int j = 0; j < grid->cols; ++j) {
			pixels.push_back(new GridPixel(grid, i, j, normInterval));
		}
	}

	return pixels;
}

Mat4 GridPixel::getObject2WorldMatrix()
{
	return this->object2WorldMatrix;
}

GridPixel::GridPixel(Grid *grid, int x, int y, std::pair<Real, Real> normInterval) : grid(grid), x(x), y(y), normInterval(normInterval) {
	update();
}

void DiffusionSimulator::initGridIntervals()
{
	GridPixel* first = pixels.at(0);
	GridPixel* last = pixels.at(pixels.size() - 1);

	grid_minX = first->pos.x;
	grid_maxX = last->pos.x;

	grid_minZ = first->pos.z;
	grid_maxZ = last->pos.z;
}

std::vector<GridPixel*> DiffusionSimulator::getProjectedPixels(Vec3 position)
{
	auto out = std::vector<GridPixel*>();

	if (position.x < grid_minX || position.x > grid_maxX || position.z < grid_minZ || position.x > grid_maxZ) {
		return out;
	}

	// X -> Row; Z -> Col
	int index_row = ((position.x - grid_minX) / (grid_maxX - grid_minX)) * T.cols;
	int index_col = ((position.z - grid_minZ) / (grid_maxZ - grid_minZ)) * T.rows;

	auto delta = CULLING_PROJECTION_RADIUS / 2; // TODO

	for (int i = std::max(0, index_row - delta); i < std::min(T.rows, index_row + delta); ++i) {
		for (int j = std::max(0, index_col - delta); j < std::min(T.cols, index_col + delta); ++j) {
			out.push_back(this->pixels.at(index(i, j, T.cols)));
		}
	}

	return out;
}

// --

Grid::Grid() : rows(0), cols(0) {
}

// Constructor to initialize the matrix with dynamic size
Grid::Grid(int numRows, int numCols) : rows(numRows), cols(numCols) {
	// Allocate memory for rows
	matrix.clear();
	matrix.assign(numRows * numCols, 0);
}

Grid::Grid(int numRows, int numCols, Real *initMatrix) : Grid(numRows, numCols) {
	for (int i = 0; i < numRows; ++i) {
		for (int j = 0; j < numCols; ++j) {
			matrix.at(index(i, j, numCols)) = initMatrix[index(i, j, numCols)];
		}
	}
}

Grid::~Grid() {
	//delete matrix;
}

// Accessor and mutator functions for the matrix elements
Real Grid::get(int row, int col) const {
	return matrix.at(index(row, col, cols));
}

void Grid::set(int row, int col, Real value) {
	matrix.at(index(row, col, cols)) = value;
}

Grid Grid::operator*(const Real scalar) const {
	Grid result(rows, cols);
	for (int i = 0; i < rows; ++i) {
		for (int j = 0; j < cols; ++j) {
			result.set(i, j, get(i, j) * scalar);
		}
	}
	return result;
}

std::pair<Real, Real> Grid::getValueInterval()
{
	Real min = matrix[0], max = matrix[0];

	for each (Real value in matrix)
	{
		if (value < min) min = value;
		if (value > max) max = value;
	}

	return std::pair<Real, Real>(min, max);
}

Grid Grid::convolution(Grid window)
{
	Grid out(rows - window.rows + 1, cols - window.cols + 1);
	for (int out_i = 0; out_i < out.rows; ++out_i) {
		for (int out_j = 0; out_j < out.cols; ++out_j) {
			Real value = 0;
			for (int w_i = 0; w_i < window.rows; ++w_i) {
				for (int w_j = 0; w_j < window.cols; ++w_j) {
					value += window.get(w_i, w_j) * get(out_i + w_i, out_j + w_j);
				}
			}
			out.set(out_i, out_j, value);
		}
	}
	return out;
}

std::string Grid::to_string()
{
	std::string out = "";
	for (int i = 0; i < rows; ++i) {
		for (int j = 0; j < cols; ++j) {
			out += std::to_string(get(i, j));
			out += "\t";
		}
		out += "\n";
	}

	return out;
}

std::vector<Real> Grid::to_vector()
{
	return matrix; // TODO: CHECK!!!
}

void Grid::update_from_vector(std::vector<Real> newVector)
{
	matrix = newVector;
}

// --

DiffusionSimulator::DiffusionSimulator()
{
	m_iTestCase = 0;
	m_vfMovableObjectPos = Vec3();
	m_vfMovableObjectFinalPos = Vec3();
	m_vfRotate = Vec3();

	initSetup_PDE();

	Real window[9] = { 0.0, 1.0, 0.0, 1.0, -4.0, 1.0, 0.0, 1.0, 0.0 };
	spatial_convolution_window = Grid(3, 3, window);
}

const char * DiffusionSimulator::getTestCasesStr(){
	return "Explicit_solver";
}

void DiffusionSimulator::reset(){
		m_mouse.x = m_mouse.y = 0;
		m_trackmouse.x = m_trackmouse.y = 0;
		m_oldtrackmouse.x = m_oldtrackmouse.y = 0;
}


void DiffusionSimulator::initUI(DrawingUtilitiesClass * DUC)
{
	this->DUC = DUC;
	initSetup_RB();
	initSetup_PDE();
}

void DiffusionSimulator::notifyCaseChanged(int testCase)
{
	m_iTestCase = testCase;
	m_vfMovableObjectPos = Vec3(0, 0, 0);
	m_vfRotate = Vec3(0, 0, 0);

	switch (m_iTestCase)
	{
	case 0:
		cout << "Explicit solver!\n";
		break;
	default:
		cout << "Empty Test!\n";
		break;
	}
}


void DiffusionSimulator::diffuseTemperatureExplicit(float timeStep) {
	Grid laplace = T.convolution(spatial_convolution_window) * (1.0 / (SPATIAL_DELTA * SPATIAL_DELTA));

	// Do not touch borders -> Dirichlet Boundary 
	for (int i = 1; i < T.rows - 1; ++i) {
		for (int j = 1; j < T.cols - 1; ++j) {
			Real u_ij_new = WAVE_SPEED * WAVE_SPEED * timeStep * timeStep * laplace.get(i - 1, j - 1) + 2 * T.get(i, j) - T_t_minus_one.get(i, j);
			u_ij_new *= DAMPING;

			T_t_minus_one.set(i, j, T.get(i, j));
			T.set(i, j, u_ij_new);
		}
	}
}

void DiffusionSimulator::initSetup_PDE()
{
	this->T = Grid(GRID_DIM, GRID_DIM); // All 0
	
	for (int i = 0; i < T.rows; ++i) {
		for (int j = 0; j < T.cols; ++j) {
			T.set(i, j, 0);
		}
	}
	
	this->pixels = GridPixel::initPixelsFromGrid(&T);

	initGridIntervals();

	this->T_t_minus_one = Grid(GRID_DIM, GRID_DIM);
}

void DiffusionSimulator::updatePixels()
{
	for each (auto pixel in pixels) pixel->update();
}



void DiffusionSimulator::simulateTimestep(float timeStep)
{
	simulateTimestep_PDE(timeStep);
	simulateTimestep_RB(timeStep);
}

void DiffusionSimulator::simulateTimestep_PDE(float timeStep) {
	diffuseTemperatureExplicit(timeStep);
	updatePixels();
}


void DiffusionSimulator::drawObjects_PDE()
{
	for each (auto pixel in pixels)
	{
		pixel->draw(DUC);
	}
}

void DiffusionSimulator::drawObjects_RB() {
	Vec3 rbColor = Vec3(1, 0, 0);

	for (int i = 0; i < rigidBodies.size(); ++i) {
		auto body = rigidBodies.at(i);
		DUC->setUpLighting(Vec3(0.5, 0.5, 0.5), Vec3(0.7, 0.75, 0.9), 10, Vec3(1, 1, 1));
		DUC->drawRigidBody(body->getObject2WorldMatrix());

		for each (auto eForce in body->externalForces) {

			// Draw connection of force point and midpoint
			DUC->beginLine();
			DUC->drawLine(body->position_x, Vec3(255, 0, 0), eForce->position, Vec3(255, 0, 0));
			DUC->endLine();

			// Draw arrow of force

			DUC->beginLine();
			auto pointTo = eForce->position;
			auto pointFrom = pointTo - eForce->force;
			DUC->drawLine(pointFrom, Vec3(255, 255, 255), pointTo, Vec3(255, 255, 255));
			DUC->endLine();

			DUC->drawSphere(pointTo, Vec3(.02, .02, .02));
		}
	}

	if (duringCreationRigidBody != nullptr) {
		auto body = duringCreationRigidBody;
		// get Color from Temperature
		DUC->setUpLighting(Vec3(0.5, 0.5, 0.5), Vec3(0.7, 0.75, 0.9), 10, Vec3(1, 1, 1));
		DUC->drawRigidBody(body->getObject2WorldMatrix());
	}
}


void DiffusionSimulator::drawFrame(ID3D11DeviceContext* pd3dImmediateContext)
{
	drawObjects_PDE();
	drawObjects_RB();
}
