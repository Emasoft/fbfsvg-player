# Homebrew formula for fbfsvg-player (macOS ARM64 and Intel x86_64)
class FbfsvgPlayer < Formula
  desc "High-performance animated SVG player for the FBF.SVG vector video format"
  homepage "https://github.com/Emasoft/fbfsvg-player"
  license "BSD-3-Clause"
  version "0.1.0"

  # Architecture-specific downloads
  on_arm do
    url "https://github.com/Emasoft/fbfsvg-player/releases/download/v0.1.0/fbfsvg-player-0.1.0-macos-arm64.tar.gz"
    sha256 "ba5e33cc53625bb8eb0bca556677ecf1250da9f98a131e1241968ba9beadf26b"
  end

  on_intel do
    url "https://github.com/Emasoft/fbfsvg-player/releases/download/v0.1.0/fbfsvg-player-0.1.0-macos-x64.tar.gz"
    sha256 "PENDING_X64_BUILD"
  end

  def install
    bin.install "fbfsvg-player"
  end

  test do
    assert_match "FBF.SVG Player", shell_output("#{bin}/fbfsvg-player --help 2>&1", 1)
  end
end
