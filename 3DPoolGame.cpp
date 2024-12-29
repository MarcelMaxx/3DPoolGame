////////////////////////////////////////////////////////////////////////////////
//
// File: 3DPoolGame.cpp
//
// Original Author: 冠芒泅 Chang-hyeon Park, 
// Modified by Bong-Soo Sohn, Dong-Jun Kim, Yu-Rui Zhang
// 
// Originally programmed for Virtual LEGO. 
// Modified later to program for Virtual Billiard.
//        
////////////////////////////////////////////////////////////////////////////////

#include "d3dUtility.h"
#include <vector>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cmath>

IDirect3DDevice9 * Device = NULL;

// 窗口大小
const int Width = 1920;
const int Height = 1080;

// 球的位置初始化（16个球，按照标准开球摆放）
const float spherePos[16][2] = {
    {-2.0f, 0.0f}, // 白球位置，在发球线后
    {2.0f, .0f}, // 1号球（最前面）
    {2.2f, -0.115f}, {2.2f, 0.115f}, // 第二排
    {2.4f, -0.23f}, {2.4f, 0.0f}, {2.4f, 0.23f}, // 第三排
    {2.6f, -0.345f}, {2.6f, -0.115f}, {2.6f, 0.115f}, {2.6f, 0.345f}, // 第四排
    {2.8f, -0.46f }, {2.8f, -0.23f}, {2.8f, 0.0f}, {2.8f, 0.23f}, {2.8f, 0.46f} // 第五排
};

// 球的颜色初始化（球0~球15）
const D3DXCOLOR sphereColor[16] = {
    d3d::WHITE, // 白球
    d3d::YELLOW, d3d::BLUE, d3d::RED, d3d::PURPLE,
    d3d::ORANGE, d3d::GREEN, d3d::MAROON, d3d::BLACK,
    d3d::YELLOW, d3d::BLUE, d3d::RED, d3d::PURPLE,
    d3d::ORANGE, d3d::GREEN, d3d::MAROON
};

// 袋子的位置初始化
const float pocketPos[6][2] = {
    {-4.5f, 3.0f}, {0.0f, 3.0f}, {4.5f, 3.0f},
    {-4.5f, -3.0f}, {0.0f, -3.0f}, {4.5f, -3.0f}
};
const float POCKET_RADIUS = 0.35f;

// ----------------------------------------------------------------------------
// 变换矩阵
// ----------------------------------------------------------------------------
D3DXMATRIX g_mWorld;
D3DXMATRIX g_mView;
D3DXMATRIX g_mProj;

#define M_RADIUS 0.15f   // 球半径
#define PI 3.14159265f
#define M_HEIGHT 0.01f
#define DECREASE_RATE 0.998f //摩擦力
#define EPSILON 0.0001f

// 球杆控制
float g_shotPower = 0.1f;
float g_maxShotPower = 5.0f;
bool g_isCharging = false; //蓄力
float g_cueOffset = 0.0f; // 球杆相对于白球的后移距离
const float g_maxCueOffset = 2.0f; // 球杆后移的最大距离

// 当前玩家
int g_currentPlayer = 1;

// 游戏状态
enum GameState {
    GAME_RUNNING,
    GAME_OVER
};
GameState g_gameState = GAME_RUNNING;

// 球杆是否可见
bool g_cueVisible = true;

// 球杆的目标位置
POINT g_mousePos;

// 发球线位置
const float g_breakLine = -2.0f;

// ----------------------------------------------------------------------------
// 颜色比较函数
// ----------------------------------------------------------------------------
bool isColorEqual(const D3DCOLORVALUE& c1, const D3DCOLORVALUE& c2)
{
    return (c1.r == c2.r) && (c1.g == c2.g) && (c1.b == c2.b) && (c1.a == c2.a);
}

// ----------------------------------------------------------------------------
// CSphere 类定义
// ----------------------------------------------------------------------------

class CSphere {
private:
    float center_x, center_y, center_z;
    float m_radius;
    float m_velocity_x;
    float m_velocity_z;
    bool  m_visible;

public:
    int   m_number;

public:
    CSphere(void)
    {
        D3DXMatrixIdentity(&m_mLocal);
        ZeroMemory(&m_mtrl, sizeof(m_mtrl));
        m_radius = M_RADIUS;
        m_velocity_x = 0;
        m_velocity_z = 0;
        m_pSphereMesh = NULL;
        m_visible = true;
        m_number = 0;
    }
    ~CSphere(void) {}

public:
    bool create(IDirect3DDevice9* pDevice, D3DXCOLOR color = d3d::WHITE)
    {
        if (NULL == pDevice)
            return false;

        m_mtrl.Ambient = color;
        m_mtrl.Diffuse = color;
        m_mtrl.Specular = color;
        m_mtrl.Emissive = d3d::BLACK;
        m_mtrl.Power = 5.0f;

        if (FAILED(D3DXCreateSphere(pDevice, getRadius(), 50, 50, &m_pSphereMesh, NULL)))
            return false;
        return true;
    }

    void destroy(void)
    {
        if (m_pSphereMesh != NULL) {
            m_pSphereMesh->Release();
            m_pSphereMesh = NULL;
        }
    }

    void draw(IDirect3DDevice9* pDevice, const D3DXMATRIX& mWorld)
    {
        if (NULL == pDevice || !m_visible)
            return;
        pDevice->SetTransform(D3DTS_WORLD, &mWorld);
        pDevice->MultiplyTransform(D3DTS_WORLD, &m_mLocal);
        pDevice->SetMaterial(&m_mtrl);
        m_pSphereMesh->DrawSubset(0);
    }

    bool hasIntersected(CSphere& ball)
    {
        if (!m_visible || !ball.m_visible)
            return false;

        D3DXVECTOR3 pos1 = this->getCenter();
        D3DXVECTOR3 pos2 = ball.getCenter();

        double distance = sqrt(pow(pos1.x - pos2.x, 2) + pow(pos1.z - pos2.z, 2));

        if (distance <= (this->getRadius() + ball.getRadius()))
            return true;

        return false;
    }

    void hitBy(CSphere& ball)
    {
        if (!hasIntersected(ball))
            return;

        D3DXVECTOR3 pos1 = this->getCenter();
        D3DXVECTOR3 pos2 = ball.getCenter();

        double v1x = this->getVelocity_X();
        double v1z = this->getVelocity_Z();
        double v2x = ball.getVelocity_X();
        double v2z = ball.getVelocity_Z();

        double dx = pos2.x - pos1.x;
        double dz = pos2.z - pos1.z;
        double distance = sqrt(dx * dx + dz * dz);

        if (distance < EPSILON) return;

        double nx = dx / distance;
        double nz = dz / distance;

        double dvx = v2x - v1x;
        double dvz = v2z - v1z;
        double vn = dvx * nx + dvz * nz;

        if (vn > 0) return;

        //冲量公式, 决定撞击动能
        double impulse = -(0.1f + DECREASE_RATE) * vn;

        this->setPower(v1x - impulse * nx, v1z - impulse * nz);
        ball.setPower(v2x + impulse * nx, v2z + impulse * nz);

        double overlap = (this->getRadius() + ball.getRadius()) - distance;
        if (overlap > 0)
        {
            float correctionX = (float)(overlap * nx / 2);
            float correctionZ = (float)(overlap * nz / 2);
            this->setCenter(pos1.x - correctionX, pos1.y, pos1.z - correctionZ);
            ball.setCenter(pos2.x + correctionX, pos2.y, pos2.z + correctionZ);
        }
    }

    void ballUpdate(float timeDiff)
    {
        if (!m_visible)
            return;

        const float TIME_SCALE = 3.3f;
        D3DXVECTOR3 cord = this->getCenter();
        double vx = this->getVelocity_X();
        double vz = this->getVelocity_Z();

        //定义最小速度
        if (fabs(vx) > 0.05f || fabs(vz) > 0.05f)
        {
            float tX = cord.x + TIME_SCALE * timeDiff * (float)m_velocity_x;
            float tZ = cord.z + TIME_SCALE * timeDiff * (float)m_velocity_z;

            this->setCenter(tX, cord.y, tZ);

            double rate = 1 - (1 - DECREASE_RATE) * timeDiff * 400;
            if (rate < 0)
                rate = 0;
            this->setPower(getVelocity_X() * rate, getVelocity_Z() * rate);
        }
        else {
            this->setPower(0, 0);
        }
    }

    void checkPocket(std::vector<D3DXVECTOR3>& pockets)
    {
        if (!m_visible)
            return;

        D3DXVECTOR3 pos = this->getCenter();

        for (size_t i = 0; i < pockets.size(); ++i)
        {
            double dx = pos.x - pockets[i].x;
            double dz = pos.z - pockets[i].z;
            double distance = sqrt(dx * dx + dz * dz);

            if (distance <= POCKET_RADIUS)
            {
                m_visible = false;
                this->setPower(0, 0);

                if (this->m_number == 0)
                {
                    this->setCenter(0.0f, M_RADIUS, -2.0f);
                    m_visible = true;
                    this->setPower(0, 0);
                }

                break;
            }
        }
    }

    double getVelocity_X() { return this->m_velocity_x; }
    double getVelocity_Z() { return this->m_velocity_z; }

    void setPower(double vx, double vz)
    {
        const double MAX_SPEED = 3.0; // 限制最大速度
        double speed = sqrt(vx * vx + vz * vz);
        if (speed > MAX_SPEED) {
            double scale = MAX_SPEED / speed;
            vx *= scale;
            vz *= scale;
        }
        this->m_velocity_x = (float)vx;
        this->m_velocity_z = (float)vz;
    }


    void setCenter(float x, float y, float z)
    {
        D3DXMATRIX m;
        center_x = x; center_y = y; center_z = z;
        D3DXMatrixTranslation(&m, x, y, z);
        setLocalTransform(m);
    }

    float getRadius(void)  const { return m_radius; }
    const D3DXMATRIX& getLocalTransform(void) const { return m_mLocal; }
    void setLocalTransform(const D3DXMATRIX& mLocal) { m_mLocal = mLocal; }
    D3DXVECTOR3 getCenter(void) const
    {
        D3DXVECTOR3 org(center_x, center_y, center_z);
        return org;
    }

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

private:
    D3DXMATRIX              m_mLocal;
    D3DMATERIAL9            m_mtrl;
    ID3DXMesh* m_pSphereMesh;
};

// ----------------------------------------------------------------------------
// CCue 类定义（球杆）
// ----------------------------------------------------------------------------

class CCue {
private:
    D3DXMATRIX m_mLocal;
    D3DMATERIAL9 m_mtrl;
    ID3DXMesh* m_pCueMesh;
    float m_rotationAngle; // 球杆的旋转角度
    float m_power;    // 球杆的蓄力程度

public:
    CCue(void)
    {
        D3DXMatrixIdentity(&m_mLocal);
        ZeroMemory(&m_mtrl, sizeof(m_mtrl));
        m_pCueMesh = NULL;
        m_rotationAngle = 0.0f;
        m_power = 0.0f;
    }
    ~CCue(void) {}

public:
    bool create(IDirect3DDevice9* pDevice)
    {
        if (NULL == pDevice)
            return false;

        m_mtrl.Ambient = d3d::GRAY;
        m_mtrl.Diffuse = d3d::GRAY;
        m_mtrl.Specular = d3d::GRAY;
        m_mtrl.Emissive = d3d::BLACK;
        m_mtrl.Power = 5.0f;

        if (FAILED(D3DXCreateCylinder(pDevice, 0.02f, 0.02f, 5.0f, 20, 20, &m_pCueMesh, NULL)))
            return false;

        return true;
    }

    void destroy(void)
    {
        if (m_pCueMesh != NULL) {
            m_pCueMesh->Release();
            m_pCueMesh = NULL;
        }
    }

    void draw(IDirect3DDevice9* pDevice, const D3DXMATRIX& mWorld, const D3DXVECTOR3& whiteBallPos)
    {
        if (NULL == pDevice || !g_cueVisible)
            return;

        // 球杆变换矩阵
        D3DXMATRIX mTrans, mRotY, mOffset, mRotZ;
        D3DXMatrixTranslation(&mTrans, whiteBallPos.x, whiteBallPos.y, whiteBallPos.z);
        D3DXMatrixRotationY(&mRotY, m_rotationAngle);
        D3DXMatrixRotationZ(&mRotZ, PI / 2);

        // 球杆后移距离
        float distanceFromWhiteBall = -3.0f - g_cueOffset; // 包括拖动偏移量
        D3DXMatrixTranslation(&mOffset, 0.0f, 0.0f, -M_RADIUS + distanceFromWhiteBall);

        m_mLocal = mOffset * mRotZ * mRotY * mTrans;

        pDevice->SetTransform(D3DTS_WORLD, &mWorld);
        pDevice->MultiplyTransform(D3DTS_WORLD, &m_mLocal);
        pDevice->SetMaterial(&m_mtrl);
        m_pCueMesh->DrawSubset(0);
    }


    void setRotationAngle(float angle)
    {
        m_rotationAngle = angle;
    }

    float getRotationAngle() const
    {
        return m_rotationAngle;
    }

    void setPower(float power)
    {
        m_power = power;
    }

    float getPower() const
    {
        return m_power;
    }
};

// ----------------------------------------------------------------------------
// CWall 类定义
// ----------------------------------------------------------------------------

class CWall {

private:

    float                   m_x;
    float                   m_z;
    float                   m_width;
    float                   m_depth;
    float                   m_height;
    bool                    m_isVertical;

public:
    CWall(void)
    {
        D3DXMatrixIdentity(&m_mLocal);
        ZeroMemory(&m_mtrl, sizeof(m_mtrl));
        m_width = 0;
        m_depth = 0;
        m_pBoundMesh = NULL;
        m_isVertical = false;
    }
    ~CWall(void) {}
public:
    bool create(IDirect3DDevice9* pDevice, float iwidth, float iheight, float idepth, D3DXCOLOR color = d3d::WHITE)
    {
        if (NULL == pDevice)
            return false;

        m_mtrl.Ambient = color;
        m_mtrl.Diffuse = color;
        m_mtrl.Specular = color;
        m_mtrl.Emissive = d3d::BLACK;
        m_mtrl.Power = 5.0f;

        m_width = iwidth;
        m_depth = idepth;
        m_height = iheight;

        if (FAILED(D3DXCreateBox(pDevice, iwidth, iheight, idepth, &m_pBoundMesh, NULL)))
            return false;
        return true;
    }
    void destroy(void)
    {
        if (m_pBoundMesh != NULL) {
            m_pBoundMesh->Release();
            m_pBoundMesh = NULL;
        }
    }
    void draw(IDirect3DDevice9* pDevice, const D3DXMATRIX& mWorld)
    {
        if (NULL == pDevice)
            return;
        pDevice->SetTransform(D3DTS_WORLD, &mWorld);
        pDevice->MultiplyTransform(D3DTS_WORLD, &m_mLocal);
        pDevice->SetMaterial(&m_mtrl);
        m_pBoundMesh->DrawSubset(0);
    }

    bool hasIntersected(CSphere& ball)
    {
        if (!ball.isVisible())
            return false;

        D3DXVECTOR3 pos = ball.getCenter();

        if (m_isVertical) {
            if (fabs(pos.x - this->m_x) <= (M_RADIUS + m_width / 2)) {
                if (pos.z >= this->m_z - m_depth / 2 && pos.z <= this->m_z + m_depth / 2)
                    return true;
            }
        }
        else {
            if (fabs(pos.z - this->m_z) <= (M_RADIUS + m_depth / 2)) {
                if (pos.x >= this->m_x - m_width / 2 && pos.x <= this->m_x + m_width / 2)
                    return true;
            }
        }
        return false;
    }

    void hitBy(CSphere& ball)
    {
        if (!hasIntersected(ball))
            return;

        D3DXVECTOR3 pos = ball.getCenter();
        double vx = ball.getVelocity_X();
        double vz = ball.getVelocity_Z();

        if (m_isVertical)
        {
            ball.setPower(-vx * DECREASE_RATE, vz * DECREASE_RATE);

            if (pos.x < m_x)
                ball.setCenter(m_x - (m_width / 2 + ball.getRadius() + EPSILON), pos.y, pos.z);
            else
                ball.setCenter(m_x + (m_width / 2 + ball.getRadius() + EPSILON), pos.y, pos.z);
        }
        else
        {
            ball.setPower(vx * DECREASE_RATE, -vz * DECREASE_RATE);

            if (pos.z < m_z)
                ball.setCenter(pos.x, pos.y, m_z - (m_depth / 2 + ball.getRadius() + EPSILON));
            else
                ball.setCenter(pos.x, pos.y, m_z + (m_depth / 2 + ball.getRadius() + EPSILON));
        }
    }

    void setPosition(float x, float y, float z)
    {
        D3DXMATRIX m;
        this->m_x = x;
        this->m_z = z;

        D3DXMatrixTranslation(&m, x, y, z);
        setLocalTransform(m);
    }

    void setOrientation(bool isVertical)
    {
        m_isVertical = isVertical;
    }

    float getHeight(void) const { return m_height; }

private:
    void setLocalTransform(const D3DXMATRIX& mLocal) { m_mLocal = mLocal; }

    D3DXMATRIX              m_mLocal;
    D3DMATERIAL9            m_mtrl;
    ID3DXMesh* m_pBoundMesh;
};

// 新的袋子类：使用球体代替圆柱，更适合固定视角
class CPocket {
private:
    float m_x, m_z;
    float m_radius;
    D3DXMATRIX m_mLocal;
    D3DMATERIAL9 m_mtrl;
    ID3DXMesh* m_pPocketMesh;

public:
    CPocket(void) {
        D3DXMatrixIdentity(&m_mLocal);
        ZeroMemory(&m_mtrl, sizeof(m_mtrl));
        m_pPocketMesh = NULL;
    }

    ~CPocket(void) {}

public:
    bool create(IDirect3DDevice9* pDevice, float radius, D3DXCOLOR color = d3d::BLACK) {
        if (NULL == pDevice)
            return false;

        m_mtrl.Ambient = color;
        m_mtrl.Diffuse = color;
        m_mtrl.Specular = color;
        m_mtrl.Emissive = color;
        m_mtrl.Power = 5.0f;

        m_radius = radius;

        // 使用球体而非圆柱，增加视觉美感
        if (FAILED(D3DXCreateSphere(pDevice, radius, 30, 30, &m_pPocketMesh, NULL)))
            return false;

        return true;
    }

    void destroy(void) {
        if (m_pPocketMesh != NULL) {
            m_pPocketMesh->Release();
            m_pPocketMesh = NULL;
        }
    }

    void draw(IDirect3DDevice9* pDevice, const D3DXMATRIX& mWorld) {
        if (NULL == pDevice)
            return;

        pDevice->SetTransform(D3DTS_WORLD, &mWorld);
        pDevice->MultiplyTransform(D3DTS_WORLD, &m_mLocal);
        pDevice->SetMaterial(&m_mtrl);
        m_pPocketMesh->DrawSubset(0);
    }

    void setPosition(float x, float y, float z) {
        D3DXMATRIX m;
        m_x = x;
        m_z = z;
        D3DXMatrixTranslation(&m, x, y, z);
        setLocalTransform(m);
    }

    D3DXVECTOR3 getPosition() const {
        return D3DXVECTOR3(m_x, 0.0f, m_z);
    }

private:
    void setLocalTransform(const D3DXMATRIX& mLocal) { m_mLocal = mLocal; }
};

// ----------------------------------------------------------------------------
// CLight 类定义
// ----------------------------------------------------------------------------

class CLight {
public:
    CLight(void)
    {
        static DWORD i = 0;
        m_index = i++;
        D3DXMatrixIdentity(&m_mLocal);
        ::ZeroMemory(&m_lit, sizeof(m_lit));
        m_pMesh = NULL;
        m_bound._center = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
        m_bound._radius = 0.0f;
    }
    ~CLight(void) {}
public:
    bool create(IDirect3DDevice9* pDevice, const D3DLIGHT9& lit, float radius = 0.1f)
    {
        if (NULL == pDevice)
            return false;

        if (FAILED(D3DXCreateSphere(pDevice, radius, 10, 10, &m_pMesh, NULL)))
            return false;

        m_bound._center = lit.Position;
        m_bound._radius = radius;

        m_lit.Type = lit.Type;
        m_lit.Diffuse = lit.Diffuse;
        m_lit.Specular = lit.Specular;
        m_lit.Ambient = lit.Ambient;
        m_lit.Position = lit.Position;
        m_lit.Direction = lit.Direction;
        m_lit.Range = lit.Range;
        m_lit.Falloff = lit.Falloff;
        m_lit.Attenuation0 = lit.Attenuation0;
        m_lit.Attenuation1 = lit.Attenuation1;
        m_lit.Attenuation2 = lit.Attenuation2;
        m_lit.Theta = lit.Theta;
        m_lit.Phi = lit.Phi;
        return true;
    }
    void destroy(void)
    {
        if (m_pMesh != NULL) {
            m_pMesh->Release();
            m_pMesh = NULL;
        }
    }
    bool setLight(IDirect3DDevice9* pDevice, const D3DXMATRIX& mWorld)
    {
        if (NULL == pDevice)
            return false;

        D3DXVECTOR3 pos(m_bound._center);
        D3DXVec3TransformCoord(&pos, &pos, &m_mLocal);
        D3DXVec3TransformCoord(&pos, &pos, &mWorld);
        m_lit.Position = pos;

        pDevice->SetLight(m_index, &m_lit);
        pDevice->LightEnable(m_index, TRUE);
        return true;
    }

    void draw(IDirect3DDevice9* pDevice)
    {
        if (NULL == pDevice)
            return;
        D3DXMATRIX m;
        D3DXMatrixTranslation(&m, m_lit.Position.x, m_lit.Position.y, m_lit.Position.z);
        pDevice->SetTransform(D3DTS_WORLD, &m);
        pDevice->SetMaterial(&d3d::WHITE_MTRL);
        m_pMesh->DrawSubset(0);
    }

    D3DXVECTOR3 getPosition(void) const { return D3DXVECTOR3(m_lit.Position); }

private:
    DWORD               m_index;
    D3DXMATRIX          m_mLocal;
    D3DLIGHT9           m_lit;
    ID3DXMesh* m_pMesh;
    d3d::BoundingSphere m_bound;
};

// ----------------------------------------------------------------------------
// 全局变量
// ----------------------------------------------------------------------------
CWall   g_legoPlane;
CWall   g_legowall[4];
CSphere g_sphere[16]; // 16个球
CCue    g_cue;        // 球杆
CLight  g_light;

std::vector<CPocket> g_pockets;

std::vector<D3DXVECTOR3> g_pocketPositions;

// ----------------------------------------------------------------------------
// 函数
// ----------------------------------------------------------------------------

void destroyAllLegoBlock(void)
{
}

bool Setup()
{
    int i;

    D3DXMatrixIdentity(&g_mWorld);
    D3DXMatrixIdentity(&g_mView);
    D3DXMatrixIdentity(&g_mProj);

    // 创建桌面
    if (false == g_legoPlane.create(Device, 9.0f, 0.03f, 6.0f, d3d::GREEN)) return false;
    g_legoPlane.setPosition(0.0f, -0.0006f / 5, 0.0f);
    g_legoPlane.setOrientation(false);

    // 创建边界墙
    if (false == g_legowall[0].create(Device, 9.0f, 0.3f, 0.12f, d3d::DARKRED)) return false;
    g_legowall[0].setPosition(0.0f, 0.12f, 3.06f);
    g_legowall[0].setOrientation(false);
    if (false == g_legowall[1].create(Device, 9.0f, 0.3f, 0.12f, d3d::DARKRED)) return false;
    g_legowall[1].setPosition(0.0f, 0.12f, -3.06f);
    g_legowall[1].setOrientation(false);
    if (false == g_legowall[2].create(Device, 0.12f, 0.3f, 6.24f, d3d::DARKRED)) return false;
    g_legowall[2].setPosition(4.56f, 0.12f, 0.0f);
    g_legowall[2].setOrientation(true);
    if (false == g_legowall[3].create(Device, 0.12f, 0.3f, 6.24f, d3d::DARKRED)) return false;
    g_legowall[3].setPosition(-4.56f, 0.12f, 0.0f);
    g_legowall[3].setOrientation(true);

    // 创建球
    for (i = 0; i < 16; i++) {
        if (false == g_sphere[i].create(Device, sphereColor[i])) return false;
        g_sphere[i].setCenter(spherePos[i][0], M_RADIUS, spherePos[i][1]);
        g_sphere[i].setPower(0, 0);
        g_sphere[i].m_number = i; // 设置球的编号
    }

    // 创建袋子
    for (i = 0; i < 6; i++)
    {
        CPocket pocket;
        if (false == pocket.create(Device, POCKET_RADIUS, d3d::BLACK)) return false;
        pocket.setPosition(pocketPos[i][0], 0.0f, pocketPos[i][1]);
        g_pockets.push_back(pocket);
        g_pocketPositions.push_back(D3DXVECTOR3(pocketPos[i][0], 0.0f, pocketPos[i][1]));
    }

    // 创建球杆
    if (false == g_cue.create(Device)) return false;

    // 光照设置
    D3DLIGHT9 lit;
    ::ZeroMemory(&lit, sizeof(lit));
    lit.Type = D3DLIGHT_POINT;
    lit.Diffuse = d3d::WHITE * 2.5f;               // 增强漫反射光照
    lit.Specular = d3d::WHITE * 0.7f;   
    lit.Ambient = d3d::WHITE * 2.2f;        // 增强环境光
    lit.Position = D3DXVECTOR3(0.0f, 10.0f, 0.0f);
    lit.Range = 100.0f;
    lit.Attenuation0 = 0.0f;
    lit.Attenuation1 = 0.9f;
    lit.Attenuation2 = 0.0f;
    if (false == g_light.create(Device, lit))
        return false;

    // 设置摄像机（固定俯视角度）
    D3DXVECTOR3 pos(0.0f, 15.0f, 0.0f); // 摄像机在上方
    D3DXVECTOR3 target(0.0f, 0.0f, 0.0f); // 目标指向桌面中心
    D3DXVECTOR3 up(0.0f, 0.0f, -1.0f); // 向上方向为-Z轴
    D3DXMatrixLookAtLH(&g_mView, &pos, &target, &up);
    Device->SetTransform(D3DTS_VIEW, &g_mView);

    // 设置投影矩阵
    D3DXMatrixPerspectiveFovLH(&g_mProj, D3DX_PI / 4,
        (float)Width / (float)Height, 1.0f, 100.0f);
    Device->SetTransform(D3DTS_PROJECTION, &g_mProj);

    // 设置渲染状态
    Device->SetRenderState(D3DRS_LIGHTING, TRUE);
    Device->SetRenderState(D3DRS_SPECULARENABLE, TRUE);
    Device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);

    g_light.setLight(Device, g_mWorld);
    return true;
}

void Cleanup(void)
{
    g_legoPlane.destroy();
    for (int i = 0; i < 4; i++) {
        g_legowall[i].destroy();
    }
    for (size_t i = 0; i < g_pockets.size(); ++i) {
        g_pockets[i].destroy();
    }
    for (int i = 0; i < 16; i++) {
        g_sphere[i].destroy();
    }
    g_cue.destroy();
    destroyAllLegoBlock();
    g_light.destroy();
}

// 时间更新函数
bool Display(float timeDelta)
{
    int i = 0;
    int j = 0;

    if (Device)
    {
        Device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0x071236, 1.0f, 0);
        Device->BeginScene();

        bool ballsMoving = false;

        // 更新球的位置，检测与墙壁的碰撞
        for (i = 0; i < 16; i++) {
            g_sphere[i].ballUpdate(timeDelta);
            if (fabs(g_sphere[i].getVelocity_X()) > 0.01f || fabs(g_sphere[i].getVelocity_Z()) > 0.01f)
                ballsMoving = true;

            for (j = 0; j < 4; j++) { g_legowall[j].hitBy(g_sphere[i]); }
            // 检测球是否进袋
            g_sphere[i].checkPocket(g_pocketPositions);
        }

        // 检测球之间的碰撞
        for (i = 0; i < 16; i++) {
            for (j = i + 1; j < 16; j++) {
                g_sphere[i].hitBy(g_sphere[j]);
            }
        }

        // 如果球都静止了，显示球杆
        if (!ballsMoving)
        {
            g_cueVisible = true;
        }

        // 绘制桌面、墙壁、球和袋子
        g_legoPlane.draw(Device, g_mWorld);
        for (i = 0; i < 4; i++) {
            g_legowall[i].draw(Device, g_mWorld);
        }
        for (i = 0; i < 16; i++) {
            g_sphere[i].draw(Device, g_mWorld);
        }
        for (size_t i = 0; i < g_pockets.size(); ++i) {
            g_pockets[i].draw(Device, g_mWorld);
        }

        // 绘制球杆
        if (g_cueVisible)
        {
            D3DXVECTOR3 whiteBallPos = g_sphere[0].getCenter();
            g_cue.draw(Device, g_mWorld, whiteBallPos);
        }

        //g_light.draw(Device);

        Device->EndScene();
        Device->Present(0, 0, 0, 0);
        Device->SetTexture(0, NULL);
    }
    return true;
}

// 输入处理函数
LRESULT CALLBACK d3d::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static POINT oldMousePos;

    switch (msg) {
    case WM_DESTROY:
    {
        ::PostQuitMessage(0);
        break;
    }
    case WM_KEYDOWN:
    {
        if (wParam == VK_ESCAPE)
        {
            ::DestroyWindow(hwnd);
        }
        break;
    }
    case WM_LBUTTONDOWN:
    {
        // 开始蓄力
        g_isCharging = true;
        ::SetCapture(hwnd);
        oldMousePos.x = LOWORD(lParam);
        oldMousePos.y = HIWORD(lParam);
        break;
    }
    case WM_LBUTTONUP:
    {
        // 释放鼠标，击球
        if (g_isCharging)
        {
            g_isCharging = false;
            ::ReleaseCapture();

            // 计算力度
            float dx = (float)(oldMousePos.x - LOWORD(lParam));
            float dy = (float)(oldMousePos.y - HIWORD(lParam));
            float distance = sqrt(dx * dx + dy * dy);

            // 新的力度计算公式，确保后拉长度与力度成线性关系
            // 根据球杆的后移距离调整射击力度
            float power = (g_cueOffset / g_maxCueOffset) * g_maxShotPower;

            // 计算击球方向
            float angle = g_cue.getRotationAngle();
            float vx = power * sinf(angle);
            float vz = power * cosf(angle);

            // 给白球施加速度
            g_sphere[0].setPower(vx, vz);

            // 隐藏球杆
            g_cueVisible = false;

            g_cueOffset = 0.0f; // 重置球杆偏移

        }
        break;
    }

    case WM_MOUSEMOVE:
        if (g_isCharging)
        {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            // 计算鼠标拖动的总距离（二维欧几里得距离）
            int deltaX = oldMousePos.x - x;
            int deltaY = oldMousePos.y - y;
            float distance = sqrtf(deltaX * deltaX + deltaY * deltaY);

            // 根据拖动距离调整球杆后移量
            g_cueOffset = (float)distance * 0.01f; // 比例控制
            if (g_cueOffset > g_maxCueOffset) g_cueOffset = g_maxCueOffset;
            if (g_cueOffset < 0.0f) g_cueOffset = 0.0f;
        }
        else
        {
            // 原有旋转角度逻辑保持不变
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);

            D3DXVECTOR3 whiteBallPos = g_sphere[0].getCenter();
            D3DXVECTOR3 dir;
            D3DXMATRIX proj;
            Device->GetTransform(D3DTS_PROJECTION, &proj);
            POINT mouse;
            mouse.x = x;
            mouse.y = y;

            D3DVIEWPORT9 viewport;
            Device->GetViewport(&viewport);

            D3DXVec3Unproject(&dir, &D3DXVECTOR3((float)mouse.x, (float)mouse.y, 1.0f),
                &viewport, &proj, &g_mView, &g_mWorld);

            dir = whiteBallPos - dir;
            g_cue.setRotationAngle(atan2f(dir.x, dir.z));
        }
        break;


    }
    return ::DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hinstance,
    HINSTANCE prevInstance,
    PSTR cmdLine,
    int showCmd)
{
    srand(static_cast<unsigned int>(time(NULL)));

    if (!d3d::InitD3D(hinstance,
        Width, Height, true, D3DDEVTYPE_HAL, &Device))
    {
        ::MessageBox(0, "InitD3D() - FAILED", 0, 0);
        return 0;
    }

    if (!Setup())
    {
        ::MessageBox(0, "Setup() - FAILED", 0, 0);
        return 0;
    }

    d3d::EnterMsgLoop(Display);

    Cleanup();

    Device->Release();

    return 0;
}