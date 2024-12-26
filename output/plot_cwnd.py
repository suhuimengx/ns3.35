import matplotlib.pyplot as plt
import numpy as np

# 读取数据文件
data = np.loadtxt("ScpsTpNewRenoConrruptionTest-cwnd.data")

# 提取时间和拥塞窗口值
time = data[:, 0]  # 第一列：时间
new_cwnd = data[:, 1]  # 第二列：新的拥塞窗口值

# 绘图
plt.figure(figsize=(14, 6))
plt.plot(time, new_cwnd, label="Congestion Window", color="blue", marker="o", linestyle="-")
plt.title("Congestion Window Over Time", fontsize=14)
plt.xlabel("Time (s)", fontsize=12)
plt.ylabel("Congestion Window (bytes)", fontsize=12)
plt.grid(True, linestyle="--", alpha=0.7)
plt.legend(fontsize=12)
plt.tight_layout()

# 保存图像为 PNG 文件
plt.savefig("ScpsTpNewRenoCorruptionTest-cwnd.png", dpi=300)
plt.show()


