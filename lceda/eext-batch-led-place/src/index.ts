/**
 * 批量设置PCB器件位置和旋转角度 - 立创EDA专业版扩展
 *
 * 功能：
 *   - 从 CSV 文件导入器件坐标，批量设置位置和旋转角度
 *   - 导出当前PCB器件坐标为CSV文件
 *   - CSV 表头：Designator,Mid X,Mid Y,Rotation,Layer
 *
 * 菜单入口：
 *   PCB 编辑器 → 顶部菜单 "批量设置元件坐标" → "从CSV文件导入并设置元件坐标" / "导出当前元件坐标为CSV"
 */

// ============================================================
// 类型定义
// ============================================================

/** 单个器件的位置配置 */
interface ComponentPosition {
  designator: string;  // 位号，如 "LED1"
  x: number;           // X坐标（CSV中的原始值，直接传给API）
  y: number;           // Y坐标
  rotation: number;    // 旋转角度（度）
  layer?: string;      // 层："Top" / "Bottom"（大小写不敏感）
}

// ============================================================
// CSV 解析
// ============================================================

/**
 * 解析 CSV 文件内容为 ComponentPosition 数组
 *
 * 支持的 CSV 表头格式：
 *   Designator,Mid X,Mid Y,Rotation,Layer
 *
 * 容错：
 *   - 自动跳过空行和表头行
 *   - 支持 BOM 头
 *   - 字段值允许有双引号包裹
 */
function parseCSV(csvText: string): ComponentPosition[] {
  // 去除 BOM
  if (csvText.charCodeAt(0) === 0xFEFF) {
    csvText = csvText.slice(1);
  }

  const lines = csvText.split(/\r?\n/).filter(line => line.trim() !== '');
  if (lines.length === 0) {
    throw new Error('CSV 文件为空');
  }

  // 解析表头
  const headerLine = lines[0];
  const headers = splitCSVLine(headerLine).map(h => h.trim().toLowerCase());

  // 查找列索引（支持多种表头名称）
  const colDesignator = findColumnIndex(headers, ['designator', '位号', 'ref']);
  const colMidX = findColumnIndex(headers, ['mid x', 'mid_x', 'center x', 'center_x', 'x', '中心x']);
  const colMidY = findColumnIndex(headers, ['mid y', 'mid_y', 'center y', 'center_y', 'y', '中心y']);
  const colRotation = findColumnIndex(headers, ['rotation', 'rot', '旋转', 'angle']);
  const colLayer = findColumnIndex(headers, ['layer', '层', 'side']);

  if (colDesignator === -1) throw new Error('CSV 缺少 Designator 列');
  if (colMidX === -1) throw new Error('CSV 缺少 Mid X 列');
  if (colMidY === -1) throw new Error('CSV 缺少 Mid Y 列');
  if (colRotation === -1) throw new Error('CSV 缺少 Rotation 列');

  const positions: ComponentPosition[] = [];

  for (let i = 1; i < lines.length; i++) {
    const fields = splitCSVLine(lines[i]);

    const designator = (fields[colDesignator] || '').trim();
    if (!designator) continue;  // 跳过空位号行

    const x = parseFloat(fields[colMidX]);
    const y = parseFloat(fields[colMidY]);
    const rotation = parseFloat(fields[colRotation]);

    if (isNaN(x) || isNaN(y) || isNaN(rotation)) {
      console.warn(`[BatchPlace] 第 ${i + 1} 行数据异常，跳过: ${lines[i]}`);
      continue;
    }

    const pos: ComponentPosition = { designator, x, y, rotation };

    if (colLayer !== -1) {
      const layerVal = (fields[colLayer] || '').trim();
      if (layerVal) {
        pos.layer = layerVal;
      }
    }

    positions.push(pos);
  }

  return positions;
}

/**
 * 按候选名称列表查找列索引
 */
function findColumnIndex(headers: string[], candidates: string[]): number {
  for (const candidate of candidates) {
    const idx = headers.indexOf(candidate);
    if (idx !== -1) return idx;
  }
  return -1;
}

/**
 * 简易 CSV 行分割（支持双引号字段）
 */
function splitCSVLine(line: string): string[] {
  const result: string[] = [];
  let current = '';
  let inQuotes = false;

  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (ch === '"') {
      if (inQuotes && i + 1 < line.length && line[i + 1] === '"') {
        current += '"';
        i++;  // 跳过转义的双引号
      } else {
        inQuotes = !inQuotes;
      }
    } else if (ch === ',' && !inQuotes) {
      result.push(current);
      current = '';
    } else {
      current += ch;
    }
  }
  result.push(current);
  return result;
}

// ============================================================
// EDA API 交互
// ============================================================

/**
 * 将 Layer 字符串归一化为标准键，实现大小写不敏感匹配
 * 支持：Top / top / TOP / 顶层 → "Top"
 *       Bottom / bottom / BOTTOM / 底层 → "Bottom"
 */
function normalizeLayer(layer: string): string {
  const lower = layer.toLowerCase().trim();
  if (lower === 'top' || lower === '顶层' || lower === 'toplayer') return 'Top';
  if (lower === 'bottom' || lower === '底层' || lower === 'bottomlayer') return 'Bottom';
  return lower;  // 未知层原样返回
}

/**
 * 层名称 → EDA 层编号映射
 * 立创EDA专业版中：Top层=1, Bottom层=2
 */
const LAYER_MAP: Record<string, number> = {
  'Top': 1,
  'Bottom': 2,
};

/**
 * 从PCB画布中获取所有器件实例
 * 返回 Map<位号字符串, 器件实例>
 */
async function fetchAllComponents(): Promise<Map<string, any>> {
  const allComponents = await eda.pcb_PrimitiveComponent.getAll();
  const compMap = new Map<string, any>();

  for (const comp of allComponents) {
    const designator = comp.getState_Designator();
    if (designator != null) {
      compMap.set(designator, comp);
    }
  }

  return compMap;
}

/**
 * 批量设置器件位置和旋转角度
 *
 * @param positions  器件位置配置数组
 * @returns 修改结果摘要
 */
async function applyPositions(
  positions: ComponentPosition[]
): Promise<{ total: number; success: number; failed: number; skipped: number; errors: string[] }> {
  const allComponents = await fetchAllComponents();

  const result = {
    total: positions.length,
    success: 0,
    failed: 0,
    skipped: 0,
    errors: [] as string[],
  };

  console.log(`[BatchPlace] PCB中共有 ${allComponents.size} 个器件`);
  console.log(`[BatchPlace] 需要设置 ${positions.length} 个器件`);

  for (let i = 0; i < positions.length; i++) {
    const pos = positions[i];
    const comp = allComponents.get(pos.designator);

    if (!comp) {
      result.skipped++;
      result.errors.push(`${pos.designator}: 未在PCB中找到该器件`);
      continue;
    }

    const modifyProps: any = {
      x: pos.x,
      y: pos.y,
      rotation: pos.rotation,
    };

    // 处理层信息
    if (pos.layer) {
      const normalizedLayer = normalizeLayer(pos.layer);
      const layerNum = LAYER_MAP[normalizedLayer];
      if (layerNum !== undefined) {
        modifyProps.layer = layerNum;
      } else {
        console.warn(`[BatchPlace] ${pos.designator}: 未知层 "${pos.layer}"，跳过层设置`);
      }
    }

    try {
      await eda.pcb_PrimitiveComponent.modify(comp, modifyProps);
      result.success++;
    } catch (err: any) {
      result.failed++;
      result.errors.push(`${pos.designator}: ${err?.message || String(err)}`);
    }
  }

  return result;
}

/**
 * 生成结果摘要文本
 */
function formatResultMessage(result: { total: number; success: number; failed: number; skipped: number; errors: string[] }): string {
  let msg = `执行完毕！\n总计: ${result.total}  成功: ${result.success}  跳过: ${result.skipped}  失败: ${result.failed}`;
  if (result.errors.length > 0) {
    msg += `\n\n错误详情 (前10条):`;
    for (const err of result.errors.slice(0, 10)) {
      msg += `\n  - ${err}`;
    }
    if (result.errors.length > 10) {
      msg += `\n  ... 还有 ${result.errors.length - 10} 条错误`;
    }
  }
  return msg;
}

// ============================================================
// 扩展入口函数（export 导出，与 headerMenus registerFn 对应）
// ============================================================

/**
 * 扩展激活入口
 * 当扩展被加载时自动调用
 */
export function activate(status?: string, arg?: string): void {
  console.log('[BatchPlace] 扩展已激活');
}

/**
 * 从CSV文件导入并设置元件坐标
 * 对应 headerMenus 中 registerFn: "importFromCSV"
 */
export async function importFromCSV(): Promise<void> {
  try {
    console.log('[BatchPlace] 开始从CSV文件导入元件坐标...');

    // 弹出文件选择对话框，让用户选择CSV文件
    const file = await eda.sys_FileSystem.openReadFileDialog('.csv', false);

    if (!file) {
      console.log('[BatchPlace] 用户取消了文件选择');
      return;
    }

    console.log(`[BatchPlace] 选中文件: ${file.name}`);

    // 读取文件文本内容（File 对象的 text() 方法）
    const csvText = await file.text();

    if (!csvText || csvText.trim().length === 0) {
      eda.sys_Dialog.showInformationMessage('所选CSV文件为空，请检查文件内容。', '导入失败');
      return;
    }

    // 解析CSV
    const positions = parseCSV(csvText);

    if (positions.length === 0) {
      eda.sys_Dialog.showInformationMessage('CSV文件中没有有效的器件数据，请检查文件格式。\n\n要求表头：Designator,Mid X,Mid Y,Rotation,Layer', '导入失败');
      return;
    }

    console.log(`[BatchPlace] 解析到 ${positions.length} 个器件`);

    // 按 Designator 排序
    positions.sort((a, b) => {
      const numA = parseInt(a.designator.replace(/\D/g, ''), 10) || 0;
      const numB = parseInt(b.designator.replace(/\D/g, ''), 10) || 0;
      const prefixA = a.designator.replace(/\d/g, '');
      const prefixB = b.designator.replace(/\d/g, '');
      if (prefixA !== prefixB) return prefixA.localeCompare(prefixB);
      return numA - numB;
    });

    // 打印前几条预览
    console.log('[BatchPlace] 数据预览 (前5条):');
    for (const p of positions.slice(0, 5)) {
      console.log(`  ${p.designator}: x=${p.x}, y=${p.y}, rotation=${p.rotation}°${p.layer ? `, layer=${p.layer}` : ''}`);
    }
    if (positions.length > 10) console.log('  ...');

    // 执行设置
    const result = await applyPositions(positions);
    const msg = formatResultMessage(result);

    console.log(`[BatchPlace] ${msg}`);

    // 弹窗显示结果
    eda.sys_Dialog.showInformationMessage(msg, '批量设置元件坐标 - 执行结果');

  } catch (err: any) {
    const errMsg = `CSV 导入失败: ${err?.message || String(err)}`;
    console.error(`[BatchPlace] ${errMsg}`);
    eda.sys_Dialog.showInformationMessage(errMsg, '导入失败');
  }
}

/**
 * 导出当前PCB中所有元件的坐标为CSV文本
 * 对应 headerMenus 中 registerFn: "exportToCSV"
 */
export async function exportToCSV(): Promise<void> {
  try {
    console.log('[BatchPlace] 开始导出当前元件坐标...');

    const allComponents = await fetchAllComponents();
    console.log(`[BatchPlace] PCB中共 ${allComponents.size} 个器件`);

    const lines: string[] = ['Designator,Mid X,Mid Y,Rotation,Layer'];

    // 按 Designator 排序
    const sortedDesignators = Array.from(allComponents.keys()).sort((a, b) => {
      const numA = parseInt(a.replace(/\D/g, ''), 10) || 0;
      const numB = parseInt(b.replace(/\D/g, ''), 10) || 0;
      const prefixA = a.replace(/\d/g, '');
      const prefixB = b.replace(/\d/g, '');
      if (prefixA !== prefixB) return prefixA.localeCompare(prefixB);
      return numA - numB;
    });

    for (const des of sortedDesignators) {
      const comp = allComponents.get(des);
      const x = comp.getState_X();
      const y = comp.getState_Y();
      const rot = comp.getState_Rotation();
      const layer = comp.getState_Layer();
      // layer 是数字，1=Top, 2=Bottom
      const layerName = layer === 1 ? 'Top' : layer === 2 ? 'Bottom' : String(layer);
      lines.push(`${des},${x},${y},${rot},${layerName}`);
    }

    const csvContent = lines.join('\n');

    // 将CSV内容保存为文件供用户下载
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8' });
    const file = new File([blob], 'component-positions.csv', { type: 'text/csv' });
    await eda.sys_FileSystem.saveFile(file, 'component-positions.csv');

    const msg = `已导出 ${sortedDesignators.length} 个器件的坐标数据\n文件: component-positions.csv`;
    console.log(`[BatchPlace] ${msg}`);
    eda.sys_Dialog.showInformationMessage(msg, '导出元件坐标');

  } catch (err: any) {
    const errMsg = `导出失败: ${err?.message || String(err)}`;
    console.error(`[BatchPlace] ${errMsg}`);
    eda.sys_Dialog.showInformationMessage(errMsg, '导出失败');
  }
}
