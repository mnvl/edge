# SPDX-License-Identifier: MIT
# Copyright (c) 2004-2026 Manvel Avetisian

"""Contract tests for choosing which GPU the renderer runs on.

A machine with two GPUs does not necessarily have two equal GPUs.  On the
desktop this was written for, one card sits in an x16 slot and the other in a
slot the bridge caps at x4 -- a quarter of the PCIe bandwidth, which matters a
great deal to a renderer whose every frame is read back to the host.  Until
this existed there was no way to say which one to use: pick_physical_device()
took the first suitable device and that was that, and because Vulkan
enumerates in PCI bus order, "first" is decided by which slot a card is
plugged into rather than by anything the caller wants.

CUDA_VISIBLE_DEVICES does not help.  It is honoured by CUDA and ignored by
Vulkan, so a process told to use cuda:1 still rendered on GPU 0.

Selection is by UUID rather than by name or index: the two cards here report
byte-identical deviceName strings, and index is exactly the thing that
depends on slot order.  The UUID Vulkan reports for an NVIDIA device is the
same one NVML and nvidia-smi report, so `nvidia-smi --query-gpu=uuid` is a
usable source for it.
"""
from __future__ import annotations

import re
import shutil
import subprocess

import pytest

import pyedge

assert "vulkan" in pyedge.available_renderer_backends(), (
    f"pyedge built without the Vulkan backend: {pyedge.available_renderer_backends()}")

_CANONICAL_UUID = re.compile(r"^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$")


@pytest.fixture(scope="module")
def devices():
    found = pyedge.enumerate_physical_devices()
    assert found, "Vulkan reported no physical devices at all"
    return found


class TestEnumeration:
    """You cannot choose a device you cannot name."""

    def test_every_device_reports_a_name_and_a_uuid(self, devices):
        for device in devices:
            assert device["name"], f"device with no name: {device}"
            assert _CANONICAL_UUID.match(device["uuid"]), (
                f"uuid is not canonical 8-4-4-4-12 hex: {device}")

    def test_uuids_are_unique(self, devices):
        uuids = [d["uuid"] for d in devices]
        assert len(set(uuids)) == len(uuids), f"duplicate uuids: {uuids}"

    def test_it_agrees_with_nvidia_smi(self, devices):
        """The whole point of using the UUID is that it crosses APIs.

        If Vulkan's deviceUUID and NVML's stopped agreeing, the mapping from
        a cuda:N flag to a Vulkan device would silently select the wrong
        card -- which looks like nothing at all until someone profiles the
        PCIe link.  Every NVIDIA device Vulkan has must be one NVML names, so
        a machine with one must have nvidia-smi; a machine without one has
        nothing to agree on.
        """
        nvidia = {d["uuid"] for d in devices if "nvidia" in d["name"].lower()}
        if not nvidia:
            return
        assert shutil.which("nvidia-smi"), f"Vulkan has NVIDIA devices {sorted(nvidia)} and there is no nvidia-smi to check them"
        probe = subprocess.run(
            ["nvidia-smi", "--query-gpu=uuid", "--format=csv,noheader"],
            capture_output=True, text=True, timeout=60)
        assert probe.returncode == 0, (
            f"nvidia-smi failed, so this machine's mapping is unverified: "
            f"{probe.stderr}")
        nvml = {line.strip().removeprefix("GPU-").lower()
                for line in probe.stdout.splitlines() if line.strip()}
        assert nvml, "nvidia-smi listed no GPUs"

        vulkan = {d["uuid"] for d in devices}
        assert nvidia <= nvml, f"Vulkan's NVIDIA devices {sorted(nvidia - nvml)} are not NVML's {sorted(nvml)}"
        assert nvml <= vulkan, (
            f"NVML reports UUIDs Vulkan does not: {sorted(nvml - vulkan)}; "
            f"Vulkan has {sorted(vulkan)}")


class TestSelection:

    def test_a_renderer_reports_the_device_it_chose(self, devices):
        renderer = pyedge.create_renderer(64, 64, backend="vulkan")
        assert renderer.device_uuid in {d["uuid"] for d in devices}

    def test_each_device_can_be_asked_for_by_uuid(self, devices):
        """Every device, not just the default -- selecting the one that
        happens to be first would prove nothing."""
        for device in devices:
            renderer = pyedge.create_renderer(
                64, 64, backend="vulkan", device_uuid=device["uuid"])
            assert renderer.device_uuid == device["uuid"], (
                f"asked for {device['name']} ({device['uuid']}), "
                f"got {renderer.device_uuid}")

    def test_the_nvidia_smi_form_of_the_uuid_is_accepted(self, devices):
        """`nvidia-smi` prints `GPU-<uuid>`; pasting that must work."""
        target = devices[0]["uuid"]
        renderer = pyedge.create_renderer(
            64, 64, backend="vulkan", device_uuid=f"GPU-{target.upper()}")
        assert renderer.device_uuid == target

    def test_an_unknown_uuid_is_an_error(self):
        """Never a silent fall back to device 0.

        Falling back is how a run ends up on the wrong card while every log
        line says it asked for the right one -- the exact failure this
        feature exists to end.
        """
        with pytest.raises(RuntimeError, match="(?i)uuid"):
            pyedge.create_renderer(
                64, 64, backend="vulkan",
                device_uuid="00000000-0000-0000-0000-000000000000")

    def test_a_malformed_uuid_is_an_error(self):
        with pytest.raises(RuntimeError, match="(?i)uuid"):
            pyedge.create_renderer(64, 64, backend="vulkan",
                                   device_uuid="not-a-uuid")

    def test_omitting_it_keeps_the_old_behaviour(self, devices):
        """Existing callers pass nothing and must be unaffected."""
        a = pyedge.create_renderer(64, 64, backend="vulkan")
        b = pyedge.create_renderer(64, 64, backend="vulkan", device_uuid="")
        assert a.device_uuid == b.device_uuid
