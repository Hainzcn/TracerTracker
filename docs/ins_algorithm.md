# INS 管线算法原理

本文档描述 `PoseProcessor` 及其调用的 `Ahrs`、`Filters`、`MathUtils` 模块中的核心算法数学原理。

> 源码位于 [`src/ins/`](../src/ins/)，主处理流程见 [`PoseProcessor.cpp`](../src/ins/PoseProcessor.cpp)。

---

## 管线总览

每帧传感器数据经过以下阶段：

```
传感器原始数据
    │
    ▼
① 数据提取          按 config.json points 映射提取 ACC / GYR / MAG / QUAT / BARO
    │
    ▼
② 姿态初始化        首帧：从加速度(+磁力计)估算初始四元数
    │
    ▼
③ AHRS 更新         Madgwick / Mahony 滤波 或 直接使用模块四元数
    │
    ▼
④ 重力剥离          旋转加速度到世界坐标系，减去重力分量
    │
    ▼
⑤ ZUPT 检测         滑动窗口方差判定静止状态
    │
    ▼
⑥ 垂直 Kalman       融合气压计高度，抑制垂直漂移
    │
    ▼
⑦ 位移积分          线性加速度 → 速度 → 位置
    │
    ▼
⑧ 偏航修正          施加显示用偏航补偿四元数
    │
    ▼
输出信号             positionUpdated / velocityUpdated / filterQuaternionsUpdated
```

---

## 1. 姿态初始化

首帧接收到加速度数据时，通过重力向量方向估算初始姿态。

**横滚角与俯仰角**（仅需加速度计）：

$$
\phi = \text{atan2}(a_y,\ a_z)
$$

$$
\theta = \text{atan2}\!\left(-a_x,\ \sqrt{a_y^2 + a_z^2}\right)
$$

**偏航角**（需磁力计，进行倾斜补偿）：

$$
H_y = m_y \cos\phi - m_z \sin\phi
$$

$$
H_x = m_x \cos\theta + m_y \sin\theta \sin\phi + m_z \sin\theta \cos\phi
$$

$$
\psi = \text{atan2}(-H_y,\ H_x)
$$

欧拉角按 **ZYX** 顺序转换为初始四元数 $q_0 = [w, x, y, z]$：

$$
\begin{aligned}
w &= \cos\frac{\psi}{2}\cos\frac{\theta}{2}\cos\frac{\phi}{2} + \sin\frac{\psi}{2}\sin\frac{\theta}{2}\sin\frac{\phi}{2} \\
x &= \sin\frac{\phi}{2}\cos\frac{\theta}{2}\cos\frac{\psi}{2} - \cos\frac{\phi}{2}\sin\frac{\theta}{2}\sin\frac{\psi}{2} \\
y &= \cos\frac{\phi}{2}\sin\frac{\theta}{2}\cos\frac{\psi}{2} + \sin\frac{\phi}{2}\cos\frac{\theta}{2}\sin\frac{\psi}{2} \\
z &= \cos\frac{\phi}{2}\cos\frac{\theta}{2}\sin\frac{\psi}{2} - \sin\frac{\phi}{2}\sin\frac{\theta}{2}\cos\frac{\psi}{2}
\end{aligned}
$$

对应实现：[`MathUtils::initializeOrientation()`](../src/ins/MathUtils.cpp)

---

## 2. Madgwick 梯度下降滤波器

每帧执行以下步骤更新姿态四元数。

### 2.1 6DOF（加速度计 + 陀螺仪）

**步骤 1** — 归一化加速度计测量值：

$$
\hat{a} = \frac{a}{\|a\|}
$$

**步骤 2** — 构造目标函数。令 $q = [q_0, q_1, q_2, q_3]$，定义重力误差函数 $f(q, \hat{a})$ 及其关于 $q$ 的梯度 $\nabla f$。梯度的四个分量 $s_0 \ldots s_3$ 由四元数旋转矩阵的偏导数展开得到，本质是将"估算重力方向"与"测量重力方向"的差异投影到四元数空间。

**步骤 3** — 归一化梯度：

$$
\hat{s} = \frac{\nabla f}{\|\nabla f\|}
$$

**步骤 4** — 四元数微分方程积分（陀螺仪积分 + 梯度修正）：

$$
\dot{q} = \frac{1}{2} q \otimes \omega - \beta \hat{s}
$$

$$
q_{t+1} = \text{normalize}(q_t + \dot{q} \cdot \Delta t)
$$

其中 $\omega = [0, g_x, g_y, g_z]$ 为陀螺仪角速度纯四元数，$\beta$ 为滤波器增益参数（`config.ins.madgwick.beta`），控制加速度计修正力度与陀螺仪积分之间的权衡。$\beta$ 越大，加速度计权重越高，对动态加速度越敏感。

### 2.2 9DOF 扩展（加速度计 + 陀螺仪 + 磁力计）

在梯度中加入磁力计约束。先将磁力计测量值通过当前四元数旋转到地球坐标系：

$$
\mathbf{h} = q \otimes [0, m_x, m_y, m_z] \otimes q^*
$$

提取参考磁场方向（消除倾斜影响，仅保留水平分量和垂直分量）：

$$
b_x = \sqrt{h_x^2 + h_y^2}, \quad b_z = h_z
$$

然后在目标函数中同时优化重力约束和磁场约束，梯度 $s_0 \ldots s_3$ 包含两组偏导数的叠加。当磁力计模为零时自动回退到 6DOF。

对应实现：[`Ahrs::madgwickUpdate6dof()`](../src/ins/Ahrs.cpp)、[`Ahrs::madgwickUpdate9dof()`](../src/ins/Ahrs.cpp)

---

## 3. Mahony 互补滤波器

Mahony 滤波器通过叉积误差反馈方式融合加速度计与陀螺仪。

### 3.1 6DOF

**步骤 1** — 从当前四元数估算重力方向（旋转矩阵第三列）：

$$
\hat{v} = \begin{bmatrix} 2(q_1 q_3 - q_0 q_2) \\ 2(q_0 q_1 + q_2 q_3) \\ q_0^2 - q_1^2 - q_2^2 + q_3^2 \end{bmatrix}
$$

**步骤 2** — 计算测量重力与估算重力之间的叉积误差：

$$
e = \hat{a} \times \hat{v}
$$

**步骤 3** — PI 控制器修正陀螺仪读数：

$$
\omega' = \omega + K_p \cdot e + K_i \int e \, dt
$$

其中 $K_p$（`config.ins.mahony.kp`）为比例增益，$K_i$（`config.ins.mahony.ki`）为积分增益，积分项在内部状态 `integralFb` 中累积。

**步骤 4** — 四元数积分：

$$
q_{t+1} = \text{normalize}\!\left(q_t + \frac{1}{2} q_t \otimes \omega' \cdot \Delta t\right)
$$

### 3.2 9DOF 扩展

误差项叠加磁力计叉积误差：

$$
e = (\hat{a} \times \hat{v}) + (\hat{m} \times \hat{w})
$$

其中 $\hat{w}$ 为四元数估算的磁场参考方向，$\hat{m}$ 为归一化后的磁力计测量值。当磁力计模为零时自动回退到 6DOF。

对应实现：[`Ahrs::mahonyUpdate6dof()`](../src/ins/Ahrs.cpp)、[`Ahrs::mahonyUpdate9dof()`](../src/ins/Ahrs.cpp)

---

## 4. 重力剥离与线性加速度

利用当前姿态四元数将机体坐标系加速度旋转到世界坐标系，然后减去重力分量：

$$
a_{\text{earth}} = R(q) \cdot a_{\text{body}}
$$

$$
a_{\text{linear}} = a_{\text{earth}} - \begin{bmatrix} 0 \\ 0 \\ g \end{bmatrix}
$$

其中 $R(q)$ 为四元数对应的 $3 \times 3$ 旋转矩阵，直接展开计算以避免两次四元数乘法的开销：

$$
R(q) = \begin{bmatrix}
1-2(y^2+z^2) & 2(xy-wz) & 2(xz+wy) \\
2(xy+wz) & 1-2(x^2+z^2) & 2(yz-wx) \\
2(xz-wy) & 2(yz+wx) & 1-2(x^2+y^2)
\end{bmatrix}
$$

$g$ 取自配置 `config.gravity_reference`（默认 9.80 m/s²）。

对应实现：[`MathUtils::rotateVector()`](../src/ins/MathUtils.cpp)

---

## 5. 位移积分

对线性加速度进行二次欧拉积分得到速度和位置：

$$
v_{t+1} = v_t + a_{\text{linear}} \cdot \Delta t
$$

$$
p_{t+1} = p_t + v_{t+1} \cdot \Delta t
$$

- 水平通道（X/Y）始终使用上述欧拉积分。
- 垂直通道（Z）可切换为 Kalman 滤波模式（见第 7 节），此时 $p_z$ 和 $v_z$ 由 Kalman 状态输出。
- $\Delta t$ 由相邻帧时间戳差值计算，首帧默认 10ms，最大限制 100ms 防止发散。

---

## 6. ZUPT 零速更新

基于滑动窗口方差检测静止状态。在窗口大小 $N$ 内，分别计算加速度模平方 $\|a\|^2$ 的方差与陀螺仪模 $\|\omega\|$ 的方差：

$$
\sigma^2 = \frac{1}{N}\sum_{i=1}^{N}(x_i - \bar{x})^2
$$

判定条件：

$$
\text{stationary} = (\sigma^2_a < \tau_a) \wedge (\sigma^2_g < \tau_g)
$$

当判定为静止时：
- **水平速度**：直接归零 $v_x = v_y = 0$
- **垂直速度**：
  - Kalman 模式：调用 `applyZupt()` 将速度状态置零并收缩协方差（$P_{11} = 10^{-4}$，速度相关行列乘以 0.01）
  - 非 Kalman 模式：直接归零 $v_z = 0$

对应实现：[`ZUPTDetector`](../src/ins/Filters.cpp)

---

## 7. 垂直 Kalman 滤波器

三状态线性 Kalman 滤波器，状态向量 $\mathbf{x} = [h,\ v,\ a]^T$（高度、速度、加速度）。

### 7.1 状态转移矩阵

匀加速运动学模型：

$$
F = \begin{bmatrix}
1 & \Delta t & \frac{1}{2}\Delta t^2 \\
0 & 1 & \Delta t \\
0 & 0 & 1
\end{bmatrix}
$$

### 7.2 过程噪声

$$
Q = \sigma_a^2 \cdot G G^T, \quad G = \begin{bmatrix} \frac{1}{2}\Delta t^2 \\ \Delta t \\ 1 \end{bmatrix}
$$

其中 $\sigma_a$ 为加速度过程噪声标准差（`config.ins.kalman.process_noise_sigma`）。

### 7.3 预测步

$$
\mathbf{x}^- = F \mathbf{x}
$$

加速度状态 $x_2$ 直接注入 IMU 垂直线性加速度 $a_z$（非状态转移产生）。

$$
P^- = F P F^T + Q
$$

### 7.4 观测模型

仅观测高度（气压计融合）：

$$
H = [1,\ 0,\ 0]
$$

### 7.5 更新步

当气压计高度可用时执行：

$$
y = z_{\text{baro}} - h^-
$$

$$
S = P^-_{00} + R
$$

$$
K = \frac{P^- H^T}{S} = \frac{1}{S}\begin{bmatrix} P^-_{00} \\ P^-_{10} \\ P^-_{20} \end{bmatrix}
$$

$$
\mathbf{x} = \mathbf{x}^- + K y
$$

$$
P = (I - KH) P^-
$$

其中 $R$ 为观测噪声方差（`config.ins.kalman.measurement_noise_R`）。

### 7.6 气压计低通滤波

气压计原始高度经一阶 IIR 低通滤波后使用：

$$
y_n = \alpha \cdot x_n + (1-\alpha) \cdot y_{n-1}
$$

$\alpha$ 取自配置 `config.ins.baro_lpf_alpha`。首个样本直接作为初始值，同时记录为气压基准高度 $h_{\text{ref}}$，后续观测值为 $\Delta h = h_{\text{filtered}} - h_{\text{ref}}$。

对应实现：[`VerticalKalmanFilter`](../src/ins/Filters.cpp)、[`LowPassFilter`](../src/ins/Filters.cpp)

---

## 8. 偏航角显示修正

最终输出四元数前，施加一个绕 Z 轴旋转的修正四元数（可配置角度 $\delta$，`config.ins.filter_yaw_offset_deg`）：

$$
q_{\text{yaw}} = \left[\cos\frac{\delta}{2},\ 0,\ 0,\ \sin\frac{\delta}{2}\right]
$$

$$
q_{\text{display}} = q_{\text{yaw}} \otimes q_{\text{filter}}
$$

这不影响位移积分（积分使用原始 $q$），仅调整 Madgwick/Mahony 四元数的显示朝向，用于补偿传感器安装方向与期望显示方向之间的偏差。

---
