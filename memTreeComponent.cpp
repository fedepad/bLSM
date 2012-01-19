/*
 * memTreeComponent.cpp
 *
 * Copyright 2009-2012 Yahoo! Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include "memTreeComponent.h"
#include "datatuple.h"

void memTreeComponent::tearDownTree(rbtree_ptr_t tree) {
    datatuple * t = 0;
    rbtree_t::iterator old;
    for(rbtree_t::iterator delitr  = tree->begin();
                           delitr != tree->end();
                           delitr++) {
    	if(t) {
    		tree->erase(old);
    		datatuple::freetuple(t);
    		t = 0;
    	}
    	t = *delitr;
    	old = delitr;
    }
	if(t) {
		tree->erase(old);
		datatuple::freetuple(t);
	}
    delete tree;
}
