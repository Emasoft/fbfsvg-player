# Homebrew formula for fbfsvg-player (macOS ARM64)
class FbfsvgPlayer < Formula
  desc "High-performance animated SVG player for the FBF.SVG vector video format"
  homepage "https://github.com/Emasoft/fbfsvg-player"
  url "https://github.com/Emasoft/fbfsvg-player/releases/download/v0.1.0/fbfsvg-player-0.1.0-macos-arm64.tar.gz"
  sha256 "ba5e33cc53625bb8eb0bca556677ecf1250da9f98a131e1241968ba9beadf26b"
  license "BSD-3-Clause"
  version "0.1.0"

  depends_on arch: :arm64

  def install
    bin.install "fbfsvg-player"
  end

  test do
    assert_match "FBF.SVG Player", shell_output("#{bin}/fbfsvg-player --help 2>&1", 1)
  end
end
