/*
Copyright (C) 2023 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

#include <test/helpers/threads_helpers.h>

/*=============================== EXECVE ===========================*/

TEST_F(sinsp_with_test_input, EXECVE_from_a_not_leader_thread)
{
	/* Instantiate the default tree */
	DEFAULT_TREE

	/* `p2_t2` calls an execve and `p2_t1` will take control in the exit event */
	generate_execve_enter_and_exit_event(0, p2_t2_tid, p2_t1_tid, p2_t1_pid, p2_t1_ptid);

	/* we should have just one thread alive, the leader one */
	ASSERT_THREAD_GROUP_INFO(p2_t1_pid, 1, false, 3, 1, p2_t1_tid);

	/* we shouldn't be able to find other threads in the thread table */
	ASSERT_MISSING_THREAD_INFO(p2_t2_tid, true);
	ASSERT_MISSING_THREAD_INFO(p2_t3_tid, true);
}

TEST_F(sinsp_with_test_input, EXECVE_from_a_leader_thread)
{
	/* Instantiate the default tree */
	DEFAULT_TREE

	/* `p2_t1` calls an execve */
	generate_execve_enter_and_exit_event(0, p2_t1_tid, p2_t1_tid, p2_t1_pid, p2_t1_ptid);

	/* we should have just one thread alive, the leader one */
	ASSERT_THREAD_GROUP_INFO(p2_t1_pid, 1, false, 3, 1, p2_t1_tid);

	/* we shouldn't be able to find other threads in the thread table */
	ASSERT_MISSING_THREAD_INFO(p2_t2_tid, true);
	ASSERT_MISSING_THREAD_INFO(p2_t3_tid, true);
}

TEST_F(sinsp_with_test_input, EXECVE_from_a_not_leader_thread_with_a_child)
{
	/* Instantiate the default tree */
	DEFAULT_TREE

	/* Create a child for `p2_t3` */
	int64_t p7_t1_tid = 100;
	UNUSED int64_t p7_t1_pid = 100;
	UNUSED int64_t p7_t1_ptid = p2_t3_tid;

	generate_clone_x_event(p7_t1_tid, p2_t3_tid, p2_t3_pid, p2_t3_ptid);
	ASSERT_THREAD_CHILDREN(p2_t3_tid, 1, 1, p7_t1_tid);

	/* Right now `p2_t1` has just one child */
	ASSERT_THREAD_CHILDREN(p2_t1_tid, 1, 1, p3_t1_tid);

	/* `p2_t2` calls an execve and `p2_t1` will take control in the exit event */
	generate_execve_enter_and_exit_event(0, p2_t2_tid, p2_t1_tid, p2_t1_pid, p2_t1_ptid);

	/* we should have just one thread alive, the leader one */
	ASSERT_THREAD_GROUP_INFO(p2_t1_pid, 1, false, 3, 1, p2_t1_tid);

	/* we shouldn't be able to find other threads in the thread table */
	ASSERT_MISSING_THREAD_INFO(p2_t2_tid, true);
	ASSERT_MISSING_THREAD_INFO(p2_t3_tid, true);

	/* Now the father of `p7_t1` should be `p2_t1` */
	ASSERT_THREAD_CHILDREN(p2_t1_tid, 2, 2, p3_t1_tid, p7_t1_tid);
}

TEST_F(sinsp_with_test_input, EXECVE_resurrect_thread)
{
	/* Instantiate the default tree */
	DEFAULT_TREE

	/* This is a corner case in which the main thread dies and after it
	 * a secondary thread of the same thread group will call an execve.
	 * This is what stress-ng does.
	 */

	/* `p2t1` dies, p2t2 is the reaper */
	remove_thread(p2_t1_tid, p2_t2_tid);
	ASSERT_THREAD_CHILDREN(p2_t2_tid, 1, 1, p3_t1_tid);
	ASSERT_THREAD_GROUP_INFO(p2_t2_pid, 2, false, 3, 3);
	auto p2_t1_tinfo = m_inspector.m_thread_manager->get_thread_ref(p2_t1_tid).get();
	ASSERT_TRUE(p2_t1_tinfo);
	/* p2t1 is present but dead */
	ASSERT_TRUE(p2_t1_tinfo->is_dead());

	/* what happens in the execve exit parser is that the main thread resurrect
	 * and it will acquire again its children, and all other threads of the group will die
	 */
	generate_execve_enter_and_exit_event(0, p2_t2_tid, p2_t1_tid, p2_t1_pid, p2_t1_ptid);

	/* The main thread is no more dead and it has again its children */
	ASSERT_FALSE(p2_t1_tinfo->is_dead());
	ASSERT_THREAD_CHILDREN(p2_t1_tid, 1, 1, p3_t1_tid);
	ASSERT_THREAD_GROUP_INFO(p2_t1_pid, 1, false, 3, 1);
	ASSERT_MISSING_THREAD_INFO(p2_t2_tid, true);
	ASSERT_MISSING_THREAD_INFO(p2_t3_tid, true);
}

TEST_F(sinsp_with_test_input, EXECVE_missing_process_execve_repair)
{
	add_default_init_thread();
	open_inspector();

	/* A process that we don't have in the table calls a random event */
	int64_t p1_t1_tid = 24;
	int64_t p1_t1_pid = 24;
	int64_t p1_t1_ptid = INIT_TID;

	/* This event should create invalid thread info for p1_t1 */
	generate_random_event(p1_t1_tid);

	/* Now we call an execve on p1_t1 */
	generate_execve_enter_and_exit_event(0, p1_t1_tid, p1_t1_tid, p1_t1_pid, p1_t1_ptid);

	/* we should have a valid thread group info and init should have a child now
	 * we are not in a container but we want to assert also vtid, vpid.
	 */
	ASSERT_THREAD_INFO_PIDS(p1_t1_tid, p1_t1_pid, p1_t1_ptid);
	ASSERT_THREAD_GROUP_INFO(p1_t1_pid, 1, false, 1, 1, p1_t1_tid);
	ASSERT_THREAD_CHILDREN(INIT_TID, 1, 1, p1_t1_tid);
}

/*=============================== EXECVE ===========================*/
