/*
 *
 * (C) COPYRIGHT 2010-2018 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

/*
 * Power policy API implementations
 */

#include <mali_kbase.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_pm.h>
#include <mali_kbase_config_defaults.h>
#include <backend/gpu/mali_kbase_pm_internal.h>

static const struct kbase_pm_policy *const all_policy_list[] = {
#ifdef CONFIG_MALI_NO_MALI
	&kbase_pm_always_on_policy_ops,
	&kbase_pm_coarse_demand_policy_ops,
#if !MALI_CUSTOMER_RELEASE
	&kbase_pm_always_on_demand_policy_ops,
#endif
#else				/* CONFIG_MALI_NO_MALI */
	&kbase_pm_coarse_demand_policy_ops,
#if !MALI_CUSTOMER_RELEASE
	&kbase_pm_always_on_demand_policy_ops,
#endif
	&kbase_pm_always_on_policy_ops
#endif /* CONFIG_MALI_NO_MALI */
};

/* A filtered list of policies available in the system, calculated by filtering
 * all_policy_list based on the flags provided by each policy.
 */
static const struct kbase_pm_policy *enabled_policy_list[ARRAY_SIZE(all_policy_list)];
static size_t enabled_policy_count;

static void generate_filtered_policy_list(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(all_policy_list); ++i) {
		const struct kbase_pm_policy *pol = all_policy_list[i];

		if (platform_power_down_only &&
				(pol->flags & KBASE_PM_POLICY_FLAG_DISABLED_WITH_POWER_DOWN_ONLY))
			continue;

		enabled_policy_list[enabled_policy_count++] = pol;
	}
}

int kbase_pm_policy_init(struct kbase_device *kbdev)
{
	generate_filtered_policy_list();
	if (enabled_policy_count == 0)
		return -EINVAL;

	kbdev->pm.backend.pm_current_policy = enabled_policy_list[0];
	kbdev->pm.backend.pm_current_policy->init(kbdev);

	return 0;
}

void kbase_pm_policy_term(struct kbase_device *kbdev)
{
	kbdev->pm.backend.pm_current_policy->term(kbdev);
}

void kbase_pm_update_active(struct kbase_device *kbdev)
{
	struct kbase_pm_device_data *pm = &kbdev->pm;
	struct kbase_pm_backend_data *backend = &pm->backend;
	unsigned long flags;
	bool active;

	lockdep_assert_held(&pm->lock);

	/* pm_current_policy will never be NULL while pm.lock is held */
	KBASE_DEBUG_ASSERT(backend->pm_current_policy);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	active = backend->pm_current_policy->get_core_active(kbdev);
	WARN((kbase_pm_is_active(kbdev) && !active),
		"GPU is active but policy '%s' is indicating that it can be powered off",
		kbdev->pm.backend.pm_current_policy->name);

	if (active) {
		/* Power on the GPU and any cores requested by the policy */
		if (!pm->backend.invoke_poweroff_wait_wq_when_l2_off &&
				pm->backend.poweroff_wait_in_progress) {
			KBASE_DEBUG_ASSERT(kbdev->pm.backend.gpu_powered);
			pm->backend.poweron_required = true;
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		} else {
			/* Cancel the the invocation of
			 * kbase_pm_gpu_poweroff_wait_wq() from the L2 state
			 * machine. This is safe - it
			 * invoke_poweroff_wait_wq_when_l2_off is true, then
			 * the poweroff work hasn't even been queued yet,
			 * meaning we can go straight to powering on.
			 */
			pm->backend.invoke_poweroff_wait_wq_when_l2_off = false;
			pm->backend.poweroff_wait_in_progress = false;
			pm->backend.l2_desired = true;

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
			kbase_pm_do_poweron(kbdev, false);
		}
	} else {
		/* It is an error for the power policy to power off the GPU
		 * when there are contexts active */
		KBASE_DEBUG_ASSERT(pm->active_count == 0);

		/* Request power off */
		if (pm->backend.gpu_powered) {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

			/* Power off the GPU immediately */
			kbase_pm_do_poweroff(kbdev, false);
		} else {
			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		}
	}
}

void kbase_pm_update_cores_state_nolock(struct kbase_device *kbdev)
{
	bool shaders_desired;

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (kbdev->pm.backend.pm_current_policy == NULL)
		return;
	if (kbdev->pm.backend.poweroff_wait_in_progress)
		return;

	if (kbdev->pm.backend.protected_transition_override)
		/* We are trying to change in/out of protected mode - force all
		 * cores off so that the L2 powers down */
		shaders_desired = false;
	else
		shaders_desired = kbdev->pm.backend.pm_current_policy->shaders_needed(kbdev);

	if (kbdev->pm.backend.shaders_desired != shaders_desired) {
		KBASE_TRACE_ADD(kbdev, PM_CORES_CHANGE_DESIRED, NULL, NULL, 0u,
				(u32)kbdev->pm.backend.shaders_desired);

		kbdev->pm.backend.shaders_desired = shaders_desired;
		kbase_pm_update_state(kbdev);
	}
}

void kbase_pm_update_cores_state(struct kbase_device *kbdev)
{
	unsigned long flags;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	kbase_pm_update_cores_state_nolock(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
}

int kbase_pm_list_policies(const struct kbase_pm_policy * const **list)
{
	WARN_ON(enabled_policy_count == 0);
	if (list)
		*list = enabled_policy_list;

	return enabled_policy_count;
}

KBASE_EXPORT_TEST_API(kbase_pm_list_policies);

const struct kbase_pm_policy *kbase_pm_get_policy(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev != NULL);

	return kbdev->pm.backend.pm_current_policy;
}

KBASE_EXPORT_TEST_API(kbase_pm_get_policy);

int set_policy_by_name(struct kbase_device *kbdev, const char *name)
{
	const struct kbase_pm_policy *new_policy = NULL;
	const struct kbase_pm_policy *const *policy_list;
	int policy_count;
	int i;

	policy_count = kbase_pm_list_policies(&policy_list);

	for (i = 0; i < policy_count; i++) {
		if (sysfs_streq(policy_list[i]->name, name)) {
			new_policy = policy_list[i];
			break;
		}
	}

	if (!new_policy) {
		printk("power_policy: policy not found\n");
		return -EINVAL;
	}

	kbase_pm_set_policy(kbdev, new_policy);

	return 0;
}

KBASE_EXPORT_TEST_API(set_policy_by_name);

void kbase_pm_set_policy(struct kbase_device *kbdev,
				const struct kbase_pm_policy *new_policy)
{
	struct kbasep_js_device_data *js_devdata = &kbdev->js_data;
	const struct kbase_pm_policy *old_policy;
	unsigned long flags;

	KBASE_DEBUG_ASSERT(kbdev != NULL);
	KBASE_DEBUG_ASSERT(new_policy != NULL);

	KBASE_TRACE_ADD(kbdev, PM_SET_POLICY, NULL, NULL, 0u, new_policy->id);

	/* During a policy change we pretend the GPU is active */
	/* A suspend won't happen here, because we're in a syscall from a
	 * userspace thread */
	kbase_pm_context_active(kbdev);

	mutex_lock(&js_devdata->runpool_mutex);
	mutex_lock(&kbdev->pm.lock);

	/* Remove the policy to prevent IRQ handlers from working on it */
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	old_policy = kbdev->pm.backend.pm_current_policy;
	kbdev->pm.backend.pm_current_policy = NULL;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	KBASE_TRACE_ADD(kbdev, PM_CURRENT_POLICY_TERM, NULL, NULL, 0u,
								old_policy->id);
	if (old_policy->term)
		old_policy->term(kbdev);

	KBASE_TRACE_ADD(kbdev, PM_CURRENT_POLICY_INIT, NULL, NULL, 0u,
								new_policy->id);
	if (new_policy->init)
		new_policy->init(kbdev);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbdev->pm.backend.pm_current_policy = new_policy;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	/* If any core power state changes were previously attempted, but
	 * couldn't be made because the policy was changing (current_policy was
	 * NULL), then re-try them here. */
	kbase_pm_update_active(kbdev);
	kbase_pm_update_cores_state(kbdev);

	mutex_unlock(&kbdev->pm.lock);
	mutex_unlock(&js_devdata->runpool_mutex);

	/* Now the policy change is finished, we release our fake context active
	 * reference */
	kbase_pm_context_idle(kbdev);
}

KBASE_EXPORT_TEST_API(kbase_pm_set_policy);
