#!/usr/bin/env bash
set -euo pipefail

### —— 配置区：请根据实际情况修改下面这些变量 —— ###

# 主仓库（GitLab URL）
MAIN_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack7/linux_for_tegra.git"
MAIN_BRANCH="R39.2.0"
MAIN_START="5f119f7c616159189aee140c3124ec9a0a92eb5e"

# 需要 subtree 合并的四个子模块配置

# 1. nvidia-oot (NVIDIA out-of-tree kernel modules, 4541 commits)
OOT_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack7/nvidia-oot.git"
OOT_BRANCH="mybranch_2026-06-12-1781256802"
OOT_PREFIX="source/nvidia-oot"

# 2. kernel-noble (Linux kernel for JetPack7, trimmed from NVIDIA init commit, ~652 commits)
KNO_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack7/kernel-noble.git"
KNO_BRANCH="mybranch_2026-06-12-1781256802"
KNO_PREFIX="source/kernel/kernel-noble"

# 3. t23x/nv-public (T23x hardware support for Orin series, 576 commits)
T23_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack7/hardware/t23x.git"
T23_BRANCH="mybranch_2026-06-12-1781256802"
T23_PREFIX="source/hardware/nvidia/t23x/nv-public"

# 4. t264/nv-public (T264 hardware support for Thor series, 473 commits)
T26_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack7/hardware/t264.git"
T26_BRANCH="mybranch_2026-06-12-1781256802"
T26_PREFIX="source/hardware/nvidia/t264/nv-public"

# GitHub 推送目标
GITHUB_URL="git@github.com:Seeed-Studio/Linux_for_Tegra.git"
GITHUB_BRANCH="r39.2.0"

# 本地临时目录
WORKDIR="$(pwd)/flatten_work"
MAIN_DIR="Linux_for_Tegra-filtered"
OOT_DIR="nvidia-oot-filtered"
KNO_DIR="kernel-noble-filtered"
T23_DIR="nv-public-t23x-filtered"
T26_DIR="nv-public-t264-filtered"

# GitHub 不允许直接推送超过 100MB 的文件，需要在历史里彻底剔除
GITHUB_STRIP_PATHS=(
  --path "nv_tegra/nvidia_drivers.tbz2"
  --path-glob "bootloader/nvidia-l4t-bootloader_*.deb"
  --path-glob "kernel/nvidia-l4t-kernel_*.deb"
  --path-glob "nv_tegra/l4t_deb_packages/nvidia-l4t-3d-core_*.deb"
)

### —— 脚本主体 —— ###

echo "============================================"
echo "  JetPack7 R39.2.0 GitLab → GitHub 同步工具"
echo "============================================"
echo ""

# 1. 清理并创建工作区
echo "📦 [1/8] 清理并创建工作区..."
rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"
cd "$WORKDIR"

# 2. 通用函数：克隆并裁剪历史
filter_history(){
  local url=$1 branch=$2 start=$3 outdir=$4
  echo ""
  echo "📋 克隆 $url @$branch → $outdir"
  git clone -b "$branch" --single-branch "$url" "$outdir"
  cd "$outdir"
  echo "✂️  裁剪历史，起始 commit: $start"
  git filter-repo --force --refs "$branch" --commit-callback '
    if not globals().get("keep", False):
        if commit.original_id == b"'"${start}"'":
            globals()["keep"] = True
        else:
            commit.skip()
  '
  local count
  count=$(git rev-list --count HEAD 2>/dev/null || echo "?")
  echo "✅ $outdir 裁剪完成，保留 $count 个 commits"
  cd ..
}

# R39.2.0 仍采用 source_sync + 覆盖四个模块的流程；子模块以当前快照导入，避免将完整历史推送到 GitHub。
clone_submodule(){
  local url=$1 branch=$2 outdir=$3
  echo ""
  echo "📋 克隆 $url @$branch → $outdir"
  git clone -b "$branch" --single-branch "$url" "$outdir"
}

strip_github_blocked_files(){
  local repo_dir=$1
  cd "$repo_dir"
  git filter-repo --force "${GITHUB_STRIP_PATHS[@]}" --invert-paths
  cd ..
}

# 3. 裁剪主仓库
echo ""
echo "📦 [2/8] 裁剪主仓库..."
filter_history "$MAIN_REPO_URL" "$MAIN_BRANCH" "$MAIN_START" "$MAIN_DIR"
strip_github_blocked_files "$MAIN_DIR"

# 4. 克隆子模块仓库
echo ""
echo "📦 [3/8] 克隆子模块仓库..."
clone_submodule "$OOT_REPO_URL" "$OOT_BRANCH" "$OOT_DIR"
clone_submodule "$KNO_REPO_URL" "$KNO_BRANCH" "$KNO_DIR"
clone_submodule "$T23_REPO_URL" "$T23_BRANCH" "$T23_DIR"
clone_submodule "$T26_REPO_URL" "$T26_BRANCH" "$T26_DIR"

# 5. 进入主仓库，先删除所有旧 submodule 目录
echo ""
echo "📦 [4/8] 删除旧 submodule 目录..."
cd "$MAIN_DIR"

# 删除所有需要移除的 submodule 目录
git rm -rf source/kernel/kernel-noble             || true
git rm -rf source/nvidia-oot                       || true
git rm -rf source/build/nvidia-public              || true
git rm -rf source/hardware/nvidia/t23x/nv-public   || true
git rm -rf source/hardware/nvidia/t264/nv-public   || true
git rm -rf source/hardware/nvidia/tegra/nv-public  || true
git rm -rf source/dtc-src/1.4.5                    || true
git rm -rf source/hwpm                             || true
git rm -rf source/kernel-devicetree                || true
git rm -rf source/nvdisplay                        || true
git rm -rf source/nvethernetrm                     || true
git rm -rf source/nvgpu                            || true
git rm -rf source/unifiedgpudisp                   || true

git commit -m "Remove old submodule directories before subtree merge" || true

# 6. 按需 subtree 合并四个模块当前快照
echo ""
echo "📦 [5/8] Subtree 合并 nvidia-oot..."
git remote add oot-filt "../$OOT_DIR"
git fetch oot-filt "$OOT_BRANCH"
git subtree add --squash --prefix="$OOT_PREFIX" oot-filt "$OOT_BRANCH"

echo "📦 Subtree 合并 kernel-noble..."
git remote add kno-filt "../$KNO_DIR"
git fetch kno-filt "$KNO_BRANCH"
git subtree add --squash --prefix="$KNO_PREFIX" kno-filt "$KNO_BRANCH"

echo "📦 Subtree 合并 t23x/nv-public..."
git remote add t23-filt "../$T23_DIR"
git fetch t23-filt "$T23_BRANCH"
git subtree add --squash --prefix="$T23_PREFIX" t23-filt "$T23_BRANCH"

echo "📦 Subtree 合并 t264/nv-public..."
git remote add t26-filt "../$T26_DIR"
git fetch t26-filt "$T26_BRANCH"
git subtree add --squash --prefix="$T26_PREFIX" t26-filt "$T26_BRANCH"

# 删除临时 remotes
for r in oot-filt kno-filt t23-filt t26-filt; do
  git remote remove "$r"
done

# 7. 拷贝 readme.md 文件到工作目录
echo ""
echo "📦 [6/8] 拷贝 readme.md..."
if [ -f "../../readme.md" ]; then
    echo "找到 readme.md，拷贝到仓库..."
    cp "../../readme.md" .
    git add readme.md
    git commit -m "Add readme.md for GitHub" || true
else
    echo "⚠️  未找到 readme.md 文件，跳过"
fi

# 8. 强制推送到 GitHub
echo ""
echo "📦 [7/8] 推送到 GitHub..."
git remote add github "$GITHUB_URL"
echo "🚀 Force-pushing to GitHub $GITHUB_BRANCH"
git push --force github "HEAD:$GITHUB_BRANCH"

# 9. 清理临时 remotes
git remote remove github

echo ""
echo "============================================"
echo "  ✅ 同步完成！"
echo "  GitHub: $GITHUB_URL"
echo "  Branch: $GITHUB_BRANCH"
echo "============================================"

