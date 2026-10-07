# CCN-DTN v2

基于 ndnSIM 的缓存数字孪生实验项目，用于比较不同缓存分配策略的业务表现和能耗。

## 1. 环境

VMware16 + Ubuntu 20.04 + Python 3.8 + ndnSIM2.9
如有绘图需求，需安装 matplotlib>=3.5。

## 2. 项目结构与文件说明

```text
ccn-dtn-v2/
├── requirements-optional.txt    # 绘图依赖
├── config/energy.conf           # 实验使用的能耗系数
├── topologies/                  # tree.txt、bottleneck.txt 两种拓扑
├── scripts/                     # 安装、运行、分析、绘图脚本
└── scratch/                     # C++ 仿真源码
```
### 脚本文件
| 文件 | 说明 |
|---|---|
| `scripts/install.py` | 将 16 个源码文件复制到 ns-3/scratch，入口由 `.cpp` 改名为 `.cc`，有差异的旧文件先备份 |
| `scripts/run_experiments.py` | 编译并按照参数设定运行仿真源码 |
| `scripts/analyze.py` | 校验输出并生成统计结果Summary |
| `scripts/plot.py` | 生成能耗分项柱图和每次运行的缓存配额曲线 |
| `scripts/sensitivity.py` | 固定已观测业务和已执行决策，缩放 DT 总成本，计算净节省 |

### 源码文件
| 文件 | 说明 |
|---|---|
| `ccn-dtn-v2.cpp` | 入口：初始化并运行仿真 |
| `experiment.hpp` | `Experiment` 类声明，用于监控网络状态 |
| `experiment-config.hpp` | 所有场景命令行参数、默认值和合法性校验 |
| `experiment-state.hpp` | 累计计数器、窗口快照、LO 历史与 EWMA 更新 |
| `experiment-network.hpp` | 创建拓扑、设定角色、流量回调、缓存配额调整 |
| `experiment-control.hpp` | D节点控制周期性查询路由器状态，发送指令控制缓存 |
| `experiment-decision.hpp` | D节点内部根据LO历史数据模拟方案并决策 |
| `experiment-metrics.hpp` | 输出各csv数据（能耗，缓存使用情况...） |
| `workload.hpp` | 按业务参数及种子预生成消费者的请求序列 |
| `replay-consumer.hpp` | 自定义Consumer Application |
| `control-app.hpp` | 自定义Control Application |
| `measured-lru.hpp` | 实际缓存 LRU 策略|
| `twin-model.hpp` | 生成Digital Twin 模型，用于D节点内部实际模拟和决策|
| `dtn-core.hpp` | |
| `energy-config.hpp` | 读取能耗配置 |
| `official-trace.hpp` | 解析Tracer输出 |

## 3. 使用示例

打开仿真项目文件夹，以ns-3源码在 `~/Desktop/ndnSIM/ns-3` 为例，运行以下命令：
```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-example \
  --modes static shadow --workload changing \
  --duration 120 --switch-time 60 --periods 5 --seeds 1
```

上面会自动编译并运行 static、shadow 模式各一次。运行完成后执行：

```bash
python3 scripts/analyze.py ./results-example
python3 scripts/plot.py ./results-example
```
注意：使用plot.py 需要安装 matplotlib>=3.5。

### 实验模式

| 模式 | 作用 |
|---|---|
| `static` | 固定均匀配额 50/50/50，作为基准 |
| `uneven` | 固定不均匀配额 80/20/50 |
| `noop` | 运行控制通信和模型，但保持配额不变 |
| `heuristic` | 根据请求率和路径长度调整配额 |
| `shadow` | 使用历史请求回放评估并调整配额 |
| `adaptive` | 在 shadow 基础上自适应调整更新周期 |
| `manual` | 在 30、60 秒手动改变配额，用于调试 |


### 批量启动参数

| 参数 | 默认值 | 怎么用 |
|---|---|---|
| `--output` | `results/` | 结果保存到哪里，如 `--output ./results-test`；重跑时换个名字 |
| `--seeds` | `1` | 每组用几个随机种子运行，如 `--seeds 10` 就用种子 1～10 各跑一次 |
| `--modes` | 除 manual 外的六种模式 | 要运行哪些模式，如 `--modes static shadow` |
| `--workload` | `changing` |`stable` 表示负载保持不变；`changing` 表示在指定时刻交换两消费者的高低负载。 |
| `--periods` | `5` | 每隔几秒更新一次缓存配额；写 `--periods 1 5 10 20` 就分别测试这四个间隔 |
| `--duration` | `120` | 产生业务请求的仿真时长，单位秒 |
| `--switch-time` | `60` | 第几秒交换负载，只用于 changing，必须大于 0 且小于 duration |
| `--energy-config` | `config/energy.conf` | 使用哪个能耗配置文件 |
| `--topology` | `topologies/tree.txt` | 使用哪个网络拓扑文件 |
| `--adaptive-initial-period` | `5` | adaptive 第一次等几秒开始更新，之后自动调整间隔 |
| `--min-period` | `1` | 更新间隔最短几秒 |
| `--max-period` | `20` | 更新间隔最长几秒 |
| `--horizon` | `30` | 模型用最近多少秒的请求记录评估配额 |
| `--dry-run` | 不启用 | 只列出准备运行的任务，不实际运行 |
| `--help` | — | 显示参数帮助 |

多个更新间隔只用于 noop、heuristic、shadow。static、uneven、manual 不按间隔重复运行；adaptive 使用自己的初始间隔。设置的间隔都要在 min-period 和 max-period 之间。

## 4.实验步骤
### 1：冒烟测试（6 次仿真）
验证脚本和环境是否正常，运行每种模式各一次：
```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-smoke \
  --workload changing --duration 12 --switch-time 6 \
  --periods 5 --seeds 1
python3 scripts/analyze.py ./results-smoke
python3 scripts/plot.py ./results-smoke
```

### 2：比较六种模式（60 次仿真）

比较默认六种模式，每种模式运行 10 个种子：

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-v2-changing-5 \
  --topology ./topologies/tree.txt \
  --workload changing --duration 120 --switch-time 60 \
  --periods 5 --seeds 10
python3 scripts/analyze.py ./results-v2-changing-5
python3 scripts/plot.py ./results-v2-changing-5
```

### 3：比较不同更新间隔（每种负载 150 次仿真）

变化负载：

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-v2-periods-changing \
  --workload changing --duration 120 --switch-time 60 \
  --periods 1 5 10 20 --seeds 10
python3 scripts/analyze.py ./results-v2-periods-changing
python3 scripts/plot.py ./results-v2-periods-changing
```

稳定负载：

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-v2-periods-stable \
  --workload stable --duration 120 \
  --periods 1 5 10 20 --seeds 10
python3 scripts/analyze.py ./results-v2-periods-stable
python3 scripts/plot.py ./results-v2-periods-stable
```

###  4：重算不同 DT 成本下的结果

在第二个实验的结果基础上，将 DT 成本分别乘以 0.25、0.5、1、2、4，不重新运行仿真：

```bash
python3 scripts/sensitivity.py ./results-v2-changing-5 \
  --scales 0.25 0.5 1 2 4
```

生成 `sensitivity.csv`。分析其他实验时，将结果目录替换为对应目录即可。

### 可选：手工改变缓存配额

```bash
python3 scripts/run_experiments.py ~/Desktop/ndnSIM/ns-3 \
  --output ./results-manual --modes manual \
  --workload changing --duration 70 --switch-time 35 \
  --periods 5 --seeds 1
python3 scripts/analyze.py ./results-manual
python3 scripts/plot.py ./results-manual
```

配额在 30 秒变为 80/20/50，在 60 秒变为 20/80/50。

## 5. 输出文件说明

每个实验组保存到 `--output` 指定的目录，每次仿真单独放在以下格式的子目录中：

```text
results-v2-changing-5/
├── topology.txt                  # 本批次拓扑副本
├── energy.conf                   # 本批次能耗系数副本
├── source_versions.json          # 依赖版本及入口源码哈希
├── static_changing_p5_seed1/      # 一次仿真的结果
├── shadow_changing_p5_seed1/
├── ...
├── all_summaries.csv              # 分析后生成
├── comparison.csv
├── paired.csv
├── energy_breakdown.png           # 绘图后生成
└── sensitivity.csv                # 敏感性分析后生成
```

### 每次仿真的输出

| 文件 | 内容与使用方法 |
|---|---|
| `run.log` | 脚本运行日志 |
| `parameters.txt` | 模式、种子、业务与模型参数、统计来源及能耗配置内容 |
| `topology.txt` | 本次仿真读取的拓扑副本 |
| `requests.csv` | 本次仿真预生成请求序列|
| `deliveries.csv` | 每个请求的成功或超时结果|
| `timeline.csv` | 每秒各节点的配额、占用、业务/控制字节、命中和操作计数。字节与计数为累计值，容量/占用不是累计量 |
| `allocations.csv` | 三个路由器的实际配额变化 |
| `lo_history.csv` | LO状态纪录|
| `control.csv` | 控制事件记录 |
| `predictions.csv` | 预测/观察命中率与误差 |
| `network-faces.csv` | 节点与网络 Face ID |
| `l3-rate-trace.txt` |  L3RateTracer 日志 |
| `app-delay-trace.txt` | AppDelayTracer 日志|
| `l2-drop-trace.txt` |  L2RateTracer 丢包统计，用于观察队列拥塞 |
| `traffic-audit.csv` | 自定义事件流量与L3RateTracer一致性验证 |
| `summary.csv` | 当前仿真的统计结果数据 |
| `cache_quotas.png` | 绘图后生成的 R1/R2/R3 配额阶梯图 |

### 分析与绘图输出

| 文件 | 用途与注意事项 |
|---|---|
| `all_summaries.csv` | 合并每次 summary 结果 |
| `comparison.csv` | 每组仿真的统计数据和比较结果 |
| `paired.csv` | 各非 static 模式与同 workload/seed 的 static 比较结果 |
| `energy_breakdown.png` | 每组仿真的能耗柱状图|
| `sensitivity.csv` | 不同DT能耗成本下的分析结果|

### 主要看哪些结果

- 看总体表现：`comparison.csv`，包括能耗、成功率和平均延迟。
- 看相对 static 是否节能：`paired.csv`，`mean_saved_j` 为正表示节省。
- 看缓存如何调整：`cache_quotas.png` 和 `allocations.csv`。
- 看控制过程和实际更新间隔：`control.csv`。
- 看逐次完整指标：`summary.csv`；能耗为配置系数下的模型估计值。

分析和绘图脚本接收的是整个实验组目录，不是某次仿真子目录。单种子实验适合验证流程，正式比较使用多种子结果。
