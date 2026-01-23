# Homebrew/Linuxbrew formula for fbfsvg-player (Linux x64)
class FbfsvgPlayerAtLinux < Formula
  desc "High-performance animated SVG player for the FBF.SVG vector video format"
  homepage "https://github.com/Emasoft/fbfsvg-player"
  url "https://github.com/Emasoft/fbfsvg-player/releases/download/v0.1.0/fbfsvg-player-0.1.0-linux-x64.tar.gz"
  sha256 "33372f71fbe6fd55dbe62343ef479efdfc243c07c5f624fe618f8a8c22be60db"
  license "BSD-3-Clause"
  version "0.1.0"

  depends_on :linux
  depends_on arch: :x86_64

  def install
    bin.install "fbfsvg-player"
  end

  test do
    assert_match "FBF.SVG Player", shell_output("#{bin}/fbfsvg-player --help 2>&1", 1)
  end
end
