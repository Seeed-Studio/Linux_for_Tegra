#!/usr/bin/env bash
set -euo pipefail

### —— 配置区：请根据实际情况修改下面这些变量 —— ###

# 主仓库（GitLab URL）
MAIN_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack/Linux_for_Tegra.git"
MAIN_BRANCH="R36.5.0"
MAIN_START="6f00dc657556aea64d05e91376c5fd5b6374004d"

# 需要 subtree 合并的三个子模块配置
OOT_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack/nvidia-oot"
OOT_BRANCH="mybranch_2026-04-13-1776072640"
OOT_START="ca81d814cea84529e637a18971e29b2c5993d061"
OOT_PREFIX="source/nvidia-oot"

KJS_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack/kernel-jammy-src"
KJS_BRANCH="mybranch_2026-04-13-1776072626"
KJS_START="20367c15a8776268765fb8b4d69f2d3fa6af3654"
KJS_PREFIX="source/kernel/kernel-jammy-src"

PUB_REPO_URL="http://192.168.1.77:1080/awesome-se/jetson/jetpack/hardware/t23x.git"
PUB_BRANCH="mybranch_2026-04-13-1776072655"
PUB_START="c6b93ee98cf0e1126c066bf50469ebbd7e33d297"
PUB_PREFIX="source/hardware/nvidia/t23x/nv-public"

# GitHub 推送目标
GITHUB_URL="git@github.com:Seeed-Studio/Linux_for_Tegra.git"
GITHUB_BRANCH="r36.5.0"

# 本地临时目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKDIR="$(pwd)/flatten_work"
MAIN_DIR="Linux_for_Tegra-filtered"
OOT_DIR="nvidia-oot-filtered"
KJS_DIR="kernel-jammy-src-filtered"
PUB_DIR="nv-public-filtered"

# GitHub 不允许直接推送超过 100MB 的文件，需要在历史里彻底剔除
GITHUB_STRIP_PATHS=(
  --path "nv_tegra/nvidia_drivers.tbz2"
  --path-glob "bootloader/nvidia-l4t-bootloader_*.deb"
  --path-glob "kernel/nvidia-l4t-kernel_*.deb"
  --path-glob "nv_tegra/l4t_deb_packages/nvidia-l4t-3d-core_*.deb"
)

### —— 脚本主体 —— ###

# 1. 清理并创建工作区
rm -rf "$WORKDIR"
mkdir -p "$WORKDIR"
cd "$WORKDIR"

# 2. 通用函数：克隆并裁剪历史
filter_history(){
  local url=$1 branch=$2 start=$3 outdir=$4
  echo "Cloning $url@$branch → $outdir"
  git clone -b "$branch" --single-branch "$url" "$outdir"
  cd "$outdir"
  echo "Filtering history from $start"
  git filter-repo --force --refs "$branch" --commit-callback '
if not globals().get("keep", False):
    if commit.original_id == b"'"${start}"'":
        globals()["keep"] = True
    else:
        commit.skip()
'
  cd ..
}

strip_github_blocked_files(){
  local repo_dir=$1
  echo "Removing GitHub-blocked large files from $repo_dir history"
  cd "$repo_dir"
  git filter-repo --force "${GITHUB_STRIP_PATHS[@]}" --invert-paths
  cd ..
}

# 3. 裁剪主仓库
filter_history "$MAIN_REPO_URL" "$MAIN_BRANCH" "$MAIN_START" "$MAIN_DIR"
strip_github_blocked_files "$MAIN_DIR"

# 4. 裁剪子模块仓库
filter_history "$OOT_REPO_URL" "$OOT_BRANCH" "$OOT_START" "$OOT_DIR"
filter_history "$KJS_REPO_URL" "$KJS_BRANCH" "$KJS_START" "$KJS_DIR"
filter_history "$PUB_REPO_URL" "$PUB_BRANCH" "$PUB_START" "$PUB_DIR"

# 5. 进入主仓库，先删除所有旧 submodule 目录
cd "$MAIN_DIR"

git rm -rf source/kernel/kernel-jammy-src    || true
git rm -rf source/nvidia-oot                  || true
git rm -rf source/hardware/nvidia/tegra/nv-public    || true
git rm -rf source/hardware/nvidia/t23x/nv-public     || true
git rm -rf source/nvdisplay                          || true
git rm -rf source/dtc-src/1.4.5                       || true
git rm -rf source/hwpm                                || true
git rm -rf source/nvgpu                               || true
git rm -rf source/nvethernetrm                        || true
git rm -rf source/kernel-devicetree                   || true

git commit -m "Remove old submodule directories before subtree merge" || true

# 6. 按需 subtree 合并三个保留历史的子模块
git remote add oot-filt "../$OOT_DIR"
git fetch oot-filt "$OOT_BRANCH"
git subtree add --prefix="$OOT_PREFIX" oot-filt "$OOT_BRANCH"

git remote add kjs-filt "../$KJS_DIR"
git fetch kjs-filt "$KJS_BRANCH"
git subtree add --prefix="$KJS_PREFIX" kjs-filt "$KJS_BRANCH"

git remote add pub-filt "../$PUB_DIR"
git fetch pub-filt "$PUB_BRANCH"
git subtree add --prefix="$PUB_PREFIX" pub-filt "$PUB_BRANCH"

# 删除临时 remotes
for r in oot-filt kjs-filt pub-filt; do
  git remote remove "$r"
done

# 7. 拷贝readme.md文件到工作目录
README_FILE="$SCRIPT_DIR/readme.md"
if [ -f "$README_FILE" ]; then
    echo "Copying readme.md to the repository..."
    cp "$README_FILE" ./readme.md
    git add readme.md
    if ! git diff --cached --quiet -- readme.md; then
        git commit -m "update readme.md file"
    else
        echo "readme.md unchanged, skipping commit."
    fi
fi

# 8. 强制推送到 GitHub
git remote add github "$GITHUB_URL"
echo "Force-pushing to GitHub $GITHUB_BRANCH"
git push --force github "HEAD:$GITHUB_BRANCH"

echo "✅ 脚本执行完成：旧 submodule 已删除，指定三个子模块历史已合并，readme.md已添加，并已强推到 GitHub。"
