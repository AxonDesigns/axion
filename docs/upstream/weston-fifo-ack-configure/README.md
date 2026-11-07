# weston: ack_configure applied before content updates queued behind a wp_fifo_v1 barrier

Draft report for the [weston issue tracker](https://gitlab.freedesktop.org/wayland/weston/-/issues).
Not filed; the AXION owner decides whether to open it.

## Summary

When a client's earlier commits are still queued behind a `wp_fifo_v1` barrier, weston 15.0.1
applies a later `xdg_surface.ack_configure` on receipt instead of with the commit that follows it.
When the queued (older) content updates are applied afterwards, they are checked against the newly
acknowledged state, and weston posts a protocol error and disconnects the client:

```
xdg_wm_base#7: error 4: xdg_surface geometry (320 x 240) does not match the configured maximized state (1280 x 688)
```

The client did nothing wrong: per xdg-shell, the acknowledgement applies to the next commit, and the
commit that followed the acknowledgement carried a buffer of exactly the configured size.

Mesa's Vulkan WSI triggers this in ordinary use. For `VK_PRESENT_MODE_FIFO_KHR` it commits each
frame with `set_barrier` and `wait_barrier`, so several frames are queued. A window that is resized
(maximized here) while presenting with FIFO is then disconnected.

## Environment

- weston 15.0.1 (Arch Linux package 15.0.1-3), `--backend=headless --renderer=pixman`
  (`weston --backend=headless --renderer=pixman --width=1280 --height=720 --idle-time=0
  --no-config`).
- libwayland 1.26.0, wayland-protocols 1.50.
- Found with Mesa 26.2.5 (lavapipe) through Dawn; reproduced without Mesa or Vulkan by the client
  below.

## Reproduction

`repro.c` and `build.sh` are in this directory: about 200 lines, using `wl_shm` buffers and
`wp_fifo_v1` only.

1. `./build.sh /tmp/weston-fifo-repro`
2. Start weston headless as above.
3. `WAYLAND_DISPLAY=<socket> /tmp/weston-fifo-repro/repro`

The client:
1. creates an xdg toplevel and acknowledges the initial configure;
2. commits six 320x240 buffers back to back, each commit with `wp_fifo_v1.set_barrier` and
   `wait_barrier`, as a FIFO swapchain does;
3. calls `xdg_toplevel.set_maximized`;
4. when the configure arrives, acknowledges it and commits a 1280x688 buffer, again with the
   barrier requests.

Result:
- Fails every time (3 of 3 runs): weston disconnects with the error above.
- Control: with `REPRO_NO_FIFO=1` the same client sends no barrier requests and runs without error.

## Protocol trace (WAYLAND_DEBUG=1, client side)

```
{Default Queue}  -> xdg_surface#9.ack_configure(1)
{Default Queue}  -> wl_shm_pool#11.create_buffer(new id wl_buffer#12, 0, 320, 240, 1280, 1)
{Default Queue}  -> wl_surface#3.attach(wl_buffer#12, 0, 0)
{Default Queue}  -> wl_surface#3.damage_buffer(0, 0, 320, 240)
{Default Queue}  -> wp_fifo_v1#8.set_barrier()
{Default Queue}  -> wp_fifo_v1#8.wait_barrier()
{Default Queue}  -> wl_surface#3.commit()
... (commits 2 to 5: the same requests with new 320x240 buffers)
{Default Queue}  -> wl_shm_pool#21.create_buffer(new id wl_buffer#22, 0, 320, 240, 1280, 1)
{Default Queue}  -> wl_surface#3.attach(wl_buffer#22, 0, 0)
{Default Queue}  -> wl_surface#3.damage_buffer(0, 0, 320, 240)
{Default Queue}  -> wp_fifo_v1#8.set_barrier()
{Default Queue}  -> wp_fifo_v1#8.wait_barrier()
{Default Queue}  -> wl_surface#3.commit()
{Default Queue}  -> xdg_toplevel#10.set_maximized()
{Default Queue} xdg_toplevel#10.configure(1280, 688, array[4])
{Default Queue} xdg_surface#9.configure(2)
{Default Queue}  -> xdg_surface#9.ack_configure(2)
{Default Queue}  -> wl_shm_pool#21.create_buffer(new id wl_buffer#19, 0, 1280, 688, 5120, 1)
{Default Queue}  -> wl_surface#3.attach(wl_buffer#19, 0, 0)
{Default Queue}  -> wl_surface#3.damage_buffer(0, 0, 1280, 688)
{Default Queue}  -> wp_fifo_v1#8.set_barrier()
{Default Queue}  -> wp_fifo_v1#8.wait_barrier()
{Default Queue}  -> wl_surface#3.commit()
{Default Queue}  -> wl_display#1.sync(new id wl_callback#17)
{Default Queue} wl_callback#17.done(2)
{Default Queue}  -> wl_display#1.sync(new id wl_callback#17)
{Display Queue} wl_display#1.error(xdg_wm_base#7, 4, "xdg_surface geometry (320 x 240) does not match the configured maximized state (1280 x 688)")
```

All six 320x240 commits were sent before `set_maximized`. The acknowledgement of configure 2 is
followed by a commit with a 1280x688 buffer. The error arrives later, once the barrier lets the
queued 320x240 updates apply.

## Expected behaviour

The acknowledgement should take effect with the first commit the client sends after it, the way
other double-buffered state does once content updates can be queued. Content updates committed
before the acknowledgement should be checked against the state that was current when they were
committed.

## Client-side workaround (what AXION does)

Before acknowledging a configure, the client waits until every earlier commit has been presented or
discarded (its `wp_presentation_feedback` has arrived). If that does not happen within a short
slice, the client skips the frame without acknowledging or committing, so the queue can only
shrink, and retries on the next frame. This delays resizes of FIFO-presented windows by roughly the
depth of the queue.
